#include <hypergrep/git_index_search.hpp>
#include <hypergrep/is_binary.hpp>
#include <unordered_set>

git_index_search::git_index_search(std::string &pattern,
                                   const std::filesystem::path &path,
                                   argparse::ArgumentParser &program)
    : basepath(std::filesystem::relative(path)) {
  initialize_search(pattern, program, options, &database, &scratch,
                    &file_filter_database, &file_filter_scratch);
}

git_index_search::git_index_search(hs_database_t *database,
                                   hs_scratch_t *scratch,
                                   hs_database_t *file_filter_database,
                                   hs_scratch_t *file_filter_scratch,
                                   const search_options &options,
                                   const std::filesystem::path &path)
    : basepath(path), database(database), scratch(scratch),
      file_filter_database(file_filter_database),
      file_filter_scratch(file_filter_scratch), options(options) {
  non_owning_database = true;
}

git_index_search::~git_index_search() {
  if (!non_owning_database) {
    if (scratch) {
      hs_free_scratch(scratch);
    }
    if (database) {
      hs_free_database(database);
    }
    if (file_filter_scratch) {
      hs_free_scratch(file_filter_scratch);
    }
    if (file_filter_database) {
      hs_free_database(file_filter_database);
    }
  }

  for (auto &iter : garbage_collect_index_iterator) {
    if (iter)
      git_index_iterator_free(iter);
  }
  for (auto &idx : garbage_collect_index) {
    if (idx)
      git_index_free(idx);
  }
  for (auto &repo : garbage_collect_repo) {
    if (repo)
      git_repository_free(repo);
  }
}

void git_index_search::search_thread_function() {
  char buffer[FILE_CHUNK_SIZE];
  std::string lines{};

  hs_scratch_t *local_scratch = NULL;
  hs_error_t database_error = hs_alloc_scratch(database, &local_scratch);
  if (database_error != HS_SUCCESS) {
    throw std::runtime_error("Error allocating scratch space\n");
  }

  while (true) {
    if (num_files_enqueued > 0) {
      try_dequeue_and_process_path(local_scratch, buffer, lines);
    }
    if (!running && num_files_dequeued == num_files_enqueued) {
      break;
    }
  }

  hs_free_scratch(local_scratch);
}

void git_index_search::run(std::filesystem::path path) {
  if (!libgit2_initialized) {
    git_libgit2_init();
    libgit2_initialized = true;
  }

  std::vector<std::thread> consumer_threads(options.num_threads);
  if (options.perform_search) {
    for (std::size_t i = 0; i < options.num_threads; ++i) {
      consumer_threads[i] = std::thread(
          std::bind(&git_index_search::search_thread_function, this));
    }
  }

  visit_git_repo(path);

  running = false;

  // Done enqueuing files for search

  // Help with the search now:
  if (options.perform_search) {
    search_thread_function();

    for (std::size_t i = 0; i < options.num_threads; ++i) {
      consumer_threads[i].join();
    }
  }

  // Process submodules
  const auto current_path = std::filesystem::current_path();
  for (const auto &sm_path : submodule_paths) {
    git_index_search git_index_searcher(
        database, scratch, file_filter_database, file_filter_scratch, options,
        basepath /
            std::filesystem::relative(std::filesystem::canonical(sm_path)));
    if (chdir(sm_path.c_str()) == 0) {
      git_index_searcher.run(".");
      if (chdir(current_path.c_str()) != 0) {
        throw std::runtime_error("Failed to restore path");
      }
    }
  }
}

bool git_index_search::process_file(const char *filename,
                                    hs_scratch_t *local_scratch, char *buffer,
                                    std::string &lines) {
  int fd = open(filename, O_RDONLY, 0);
  if (fd == -1) {
    return false;
  }
  bool result{false};

  const auto process_fn =
      (options.is_stdout || options.print_only_matching_parts ||
       options.show_column_numbers || options.show_byte_offset)
          ? process_matches
          : process_matches_nocolor_nostdout;
  auto result_path = basepath / filename;

  // Process the file in chunks
  std::size_t total_bytes_read = 0;
  bool max_file_size_provided = options.max_file_size.has_value();
  std::size_t max_file_size =
      max_file_size_provided ? options.max_file_size.value() : 0;
  std::size_t bytes_read = 0;
  std::atomic<std::size_t> max_line_number{0};
  std::size_t current_line_number{1};
  std::size_t num_matching_lines{0};
  std::size_t num_matches{0};

  // Read the file in chunks and perform search
  bool first{true};
  while (true) {

    auto ret = read(fd, buffer, FILE_CHUNK_SIZE);

    if (ret > 0) {
      bytes_read = ret;
    } else {
      break;
    }

    total_bytes_read += bytes_read;

    if (max_file_size_provided && total_bytes_read > max_file_size) {
      // File size limit reached
      close(fd);
      lines.clear();
      return false;
    }

    if (first) {
      first = false;
      if (starts_with_magic_bytes(buffer, bytes_read)) {
        result = false;
        break;
      }

      if (has_null_bytes(buffer, bytes_read)) {
        // NULL bytes found
        // Ignore file
        // Could be a .exe, .gz, .bin etc.
        result = false;
        break;
      }
    }

    // Detect end of file
    // If end of file, search entire chunk
    // If not end of file, search for last newline
    const auto last_chunk = (bytes_read < FILE_CHUNK_SIZE);
    std::size_t search_size = bytes_read;

    char *last_newline = buffer + bytes_read;
    if (!last_chunk) {
      // Find the position of the last newline in the buffer
      // In order to catch matches between chunks, need to amend the buffer
      // and make sure it stops at a new line boundary
      char *last_newline = (char *)memrchr(buffer, '\n', bytes_read);
      if (last_newline) {
        search_size = last_newline - buffer;
      } else {
        // Not a single newline in the entire chunk
        // This could be some binary file or some minified JS file
        // Skip it
        result = false;
        break;
      }
    }

    std::vector<std::pair<unsigned long long, unsigned long long>> matches{};
    std::atomic<size_t> number_of_matches = 0;
    file_context ctx{number_of_matches, matches, options.print_only_filenames};

    if (hs_scan(database, buffer, search_size, 0, local_scratch, on_match,
                (void *)(&ctx)) != HS_SUCCESS) {
      if (options.print_only_filenames && ctx.number_of_matches > 0) {
        result = true;
      } else {
        result = false;
      }
      break;
    } else {
      if (ctx.number_of_matches > 0) {
        result = true;
      }
    }

    if (ctx.number_of_matches > 0) {
      num_matching_lines += process_fn(
          result_path.c_str(), buffer, search_size, ctx.matches,
          current_line_number, lines, options.print_filenames,
          options.is_stdout, options.show_line_numbers,
          options.show_column_numbers, options.show_byte_offset,
          options.print_only_matching_parts, options.max_column_limit,
          total_bytes_read - bytes_read, options.ltrim_each_output_line);
      num_matches += ctx.number_of_matches;
    }

    if (!last_chunk) {

      if (static_cast<std::size_t>(last_newline - buffer) == bytes_read - 1) {
        // Chunk ends exactly at the newline
        // Do nothing
      } else {
        // Backtrack "remainder" number of characters
        if ((bytes_read - search_size) < FILE_CHUNK_SIZE) {
          lseek(fd, -1 * (bytes_read - search_size), SEEK_CUR);
        } else {
          // Don't lseek back the entire chunk
          // because that's an infinite loop

          // Not a single newline in the entire chunk
          // This could be some binary file or some minified JS file
          // Skip it
          result = false;
          break;
        }
      }
    }
  }

  close(fd);

  if ((result || options.count_include_zeros) && options.count_matching_lines &&
      !options.print_only_filenames) {
    if (options.print_filenames) {
      if (options.is_stdout) {
        fmt::print(
            "{}:{}\n",
            fmt::format(fg(fmt::color::steel_blue), "{}", result_path.c_str()),
            num_matching_lines);
      } else {
        fmt::print("{}:{}\n", result_path.c_str(), num_matching_lines);
      }
    } else {
      fmt::print("{}\n", num_matching_lines);
    }
  } else if ((result || options.count_include_zeros) && options.count_matches &&
             !options.print_only_filenames) {
    if (options.print_filenames) {
      if (options.is_stdout) {
        fmt::print(
            "{}:{}\n",
            fmt::format(fg(fmt::color::steel_blue), "{}", result_path.c_str()),
            num_matches);
      } else {
        fmt::print("{}:{}\n", result_path.c_str(), num_matches);
      }
    } else {
      fmt::print("{}\n", num_matches);
    }
  } else {
    if (result && options.print_only_filenames) {
      if (options.is_stdout) {
        fmt::print(fg(fmt::color::steel_blue), "{}\n", result_path.c_str());
      } else {
        fmt::print("{}\n", result_path.c_str());
      }
    } else if (result && !options.count_matching_lines &&
               !options.count_matches && !options.print_only_filenames &&
               !lines.empty()) {
      if (options.is_stdout) {
        if (options.print_filenames) {
          lines = fmt::format(fg(fmt::color::steel_blue), "\n{}\n",
                              result_path.c_str()) +
                  lines;
        } else {
          lines += "\n";
        }
        fmt::print("{}", lines);
      } else {
        fmt::print("{}", lines);
      }
    }
  }

  lines.clear();
  return result;
}

bool git_index_search::search_submodules(const char *dir,
                                         git_repository *this_repo) {

  if (options.exclude_submodules) {
    return true;
  }

  struct context {
    git_index_search *ptr;
    const char *dir;
  };

  context ctx{this, dir};

  if (git_submodule_foreach(
          this_repo,
          [](git_submodule *, const char *name, void *payload) -> int {
            context *ctx = (context *)(payload);
            auto self = ctx->ptr;
            auto dir = ctx->dir;

            auto path = std::filesystem::path(dir) / name;
            if (std::filesystem::exists(path / ".git")) {
              self->submodule_paths.push_back(path);
            } else {
              // submodule not recursively cloned
              // ignore
            }

            return 0;
          },
          (void *)&ctx) != 0) {
    return false;
  } else {
    return true;
  }
}

bool git_index_search::visit_git_index(const std::filesystem::path &,
                                       git_index *index) {
  git_index_iterator *iter{nullptr};
  if (git_index_iterator_new(&iter, index) == 0) {

    if (iter)
      garbage_collect_index_iterator.push_back(iter);

    const git_index_entry *entry = nullptr;
    while (git_index_iterator_next(&entry, iter) != GIT_ITEROVER) {
      if (entry &&
          (!options.filter_files ||
           (options.filter_files &&
            filter_file(entry->path, file_filter_database, file_filter_scratch,
                        options.negate_filter)))) {

        // Skip directories and symlinks
        if ((entry->mode & S_IFMT) == S_IFDIR ||
            (entry->mode & S_IFMT) == S_IFLNK) {
          continue;
        }

        if (!options.search_hidden_files) {
          if (entry->path[0] == '.') {
            continue;
          }
          std::string_view path(entry->path);
          auto it = path.find_last_of("/");
          if (it != std::string_view::npos) {
            // Found a '/'
            if (path[it + 1] == '.') {
              continue;
            }
          }
        }

        if (options.perform_search) {
          queue.enqueue(ptok, entry->path);
          ++num_files_enqueued;
        } else {

          // If --max-filesize is used, perform a stat
          // and check if the file size is under the limit
          static const bool max_file_size_provided =
              options.max_file_size.has_value();
          static const std::size_t max_file_size =
              max_file_size_provided ? options.max_file_size.value() : 0;

          if (max_file_size_provided) {

            // NOTE: Possible optimization here is to use entry->file_size
            // instead of making a stat call
            //
            // Making the stat call is the more accurate approach since
            // these files could have unstaged changes on disk.

            const auto file_size = std::filesystem::file_size(entry->path);
            if (file_size > max_file_size) {
              // skip this file
              continue;
            }
          }

          if (options.is_stdout) {
            fmt::print(fg(fmt::color::steel_blue), "./{}\n", entry->path);
          } else {
            fmt::print("./{}\n", entry->path);
          }
        }
      }
    }
    return true;
  } else {
    return false;
  }
}

bool git_index_search::visit_git_repo(const std::filesystem::path &dir,
                                      git_repository *repo) {
  bool result{true};

  // Open the repository
  if (!repo) {
    if (git_repository_open(&repo, dir.c_str()) != 0) {
      result = false;
    }
  }

  if (result)
    garbage_collect_repo.push_back(repo);

  // Load the git index for this repository
  git_index *index = nullptr;
  if (result && git_repository_index(&index, repo) != 0) {
    result = false;
  }

  if (result)
    garbage_collect_index.push_back(index);

  // Visit each entry in the index
  if (result && !visit_git_index(dir, index)) {
    result = false;
  }

  // Search the submodules inside this submodule
  if (result && !search_submodules(dir.c_str(), repo)) {
    result = false;
  }
  return result;
}

bool git_index_search::try_dequeue_and_process_path(hs_scratch_t *local_scratch,
                                                    char *buffer,
                                                    std::string &lines) {
  constexpr std::size_t BULK_DEQUEUE_SIZE = 32;
  const char *entries[BULK_DEQUEUE_SIZE];
  auto count =
      queue.try_dequeue_bulk_from_producer(ptok, entries, BULK_DEQUEUE_SIZE);
  if (count > 0) {
    for (std::size_t j = 0; j < count; ++j) {
      process_file(entries[j], local_scratch, buffer, lines);
    }
    num_files_dequeued += count;
    return true;
  }
  return false;
}

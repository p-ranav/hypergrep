#include <hypergrep/directory_search.hpp>
#include <hypergrep/is_binary.hpp>
#include <hypergrep/trim_whitespace.hpp>

directory_search::directory_search(std::string &pattern,
                                   const std::filesystem::path &path,
                                   argparse::ArgumentParser &program)
    : search_path(path) {
  initialize_search(pattern, program, options, &database, &scratch,
                    &file_filter_database, &file_filter_scratch);
}

directory_search::~directory_search() {
  if (file_filter_scratch) {
    hs_free_scratch(file_filter_scratch);
  }
  if (file_filter_database) {
    hs_free_database(file_filter_database);
  }
  if (scratch) {
    hs_free_scratch(scratch);
  }
  if (database) {
    hs_free_database(database);
  }
}

void directory_search::search_thread_function() {
  char buffer[FILE_CHUNK_SIZE];
  std::string lines{};

  hs_scratch_t *local_scratch = NULL;
  hs_error_t database_error = hs_alloc_scratch(database, &local_scratch);
  if (database_error != HS_SUCCESS) {
    throw std::runtime_error("Error allocating scratch space\n");
  }

  moodycamel::ConsumerToken ctok{queue};

  while (true) {
    if (num_files_enqueued > 0) {
      try_dequeue_and_process_path(ctok, local_scratch, buffer, lines);
    }
    if (!running && num_files_dequeued == num_files_enqueued) {
      break;
    }
  }

  hs_free_scratch(local_scratch);
}

void directory_search::run(std::filesystem::path path) {
  if (!options.ignore_gitindex) {
    git_libgit2_init();
  }

  std::vector<std::thread> consumer_threads(options.num_threads);
  if (options.perform_search) {
    for (std::size_t i = 0; i < options.num_threads; ++i) {
      consumer_threads[i] = std::thread(
          std::bind(&directory_search::search_thread_function, this));
    }
  }

  // Kick off the directory traversal
  moodycamel::ProducerToken ptok{queue};
  visit_directory_and_enqueue(ptok, path.string(), file_filter_scratch);

  // Spawn threads to handle subdirectories
  {
    const auto num_subdir_threads = options.num_threads;
    std::vector<std::thread> traversal_threads(num_subdir_threads);
    for (std::size_t i = 0; i < num_subdir_threads; ++i) {
      traversal_threads[i] = std::thread([this]() {
        // A local scratch for each traversal thread
        hs_scratch *local_file_filter_scratch{nullptr};
        if (options.filter_files) {
          hs_error_t database_error = hs_alloc_scratch(
              file_filter_database, &local_file_filter_scratch);
          if (database_error != HS_SUCCESS) {
            fprintf(stderr, "Error allocating scratch space\n");
            hs_free_database(file_filter_database);
            return;
          }
        }

        while (num_dirs_enqueued > 0) {
          std::string subdir{};
          auto found = subdirectories.try_dequeue(subdir);
          if (found) {
            if (!options.ignore_gitindex) {
              const auto dot_git_path = std::filesystem::path(subdir) / ".git";
              if (std::filesystem::exists(dot_git_path)) {
                // Enqueue git repo
                git_repo_paths.enqueue(subdir);
                ++num_git_repos_enqueued;
              } else {
                // go deeper
                moodycamel::ProducerToken ptok{queue};
                visit_directory_and_enqueue(ptok, subdir,
                                            local_file_filter_scratch);
              }
            } else {
              // Ignore git index
              // Just go deeper
              moodycamel::ProducerToken ptok{queue};
              visit_directory_and_enqueue(ptok, subdir,
                                          local_file_filter_scratch);
            }
            num_dirs_enqueued -= 1;
          }
        }

        if (local_file_filter_scratch) {
          hs_free_scratch(local_file_filter_scratch);
        }
      });
    }

    for (std::size_t i = 0; i < num_subdir_threads; ++i) {
      traversal_threads[i].join();
    }
  }

  running = false;

  // Done enqueuing files for search

  // Help with the search now:
  if (options.perform_search) {
    search_thread_function();

    for (std::size_t i = 0; i < options.num_threads; ++i) {
      consumer_threads[i].join();
    }
  }

  // All threads are done processing the file queue

  // Search git repos one by one
  if (!options.ignore_gitindex && num_git_repos_enqueued > 0) {
    auto current_path = std::filesystem::current_path();

    while (num_git_repos_enqueued > 0) {
      std::string repo_path{};
      auto found = git_repo_paths.try_dequeue(repo_path);
      if (found) {

        git_index_search git_index_searcher(
            database, scratch, file_filter_database, file_filter_scratch,
            options, repo_path);
        if (chdir(repo_path.c_str()) == 0) {
          git_index_searcher.run(".");
          if (chdir(current_path.c_str()) != 0) {
            throw std::runtime_error("Failed to restore path");
          }
        }

        --num_git_repos_enqueued;
      }
    }
  }

  if (options.perform_search) {
    // Now search large files one by one using the large_file_searcher
    if (num_large_files_enqueued > 0) {
      file_search large_file_searcher(database, scratch, options);

      // Memory map + multi-threaded search
      while (num_large_files_enqueued > 0) {
        large_file lf{};
        auto found = large_file_backlog.try_dequeue(lf);
        if (found) {
          large_file_searcher.run(lf.path, lf.size);
          --num_large_files_enqueued;
        }
      }
    }
  }
}

bool is_blacklisted(const std::string &str) {
  const std::vector<std::string> substrings{".o",    ".so",  ".png", ".jpg",
                                            ".jpeg", ".mp3", ".mp4", ".gz",
                                            ".xz",   ".zip"};

  for (const auto &substring : substrings) {
    if (str.size() >= substring.size() &&
        str.compare(str.size() - substring.size(), substring.size(),
                    substring) == 0) {
      return true;
    }
  }
  return false;
}

bool directory_search::process_file(std::string &&filename,
                                    hs_scratch_t *local_scratch, char *buffer,
                                    std::string &lines) {

  if (is_blacklisted(filename)) {
    return false;
  }

  int fd = open(filename.data(), O_RDONLY, 0);
  if (fd == -1) {
    return false;
  }
  bool result{false};

  const auto process_fn =
      (options.is_stdout || options.print_only_matching_parts ||
       options.show_column_numbers || options.show_byte_offset)
          ? process_matches
          : process_matches_nocolor_nostdout;

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
  bool continue_even_though_large_file{false};
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
    } else if (!continue_even_though_large_file &&
               total_bytes_read > LARGE_FILE_SIZE) {
      // This file is a bit large
      // Add it to the backlog and process it later with a file_search object
      // instead of using a single thread to read in chunks
      // The file_search object will memory map and search this large file
      // in multiple threads

      // Perform a stat and check the file size?
      // If the file size is not much larger than total_bytes_read
      // just continue and finish the file
      const auto file_size = std::filesystem::file_size(filename.data());

      // Only bail if the file size if more than twice of
      // what hypergrep has already searched
      if (total_bytes_read * 2 > file_size) {

        large_file lf{std::move(filename), file_size};

        large_file_backlog.enqueue(lf);
        ++num_large_files_enqueued;
        close(fd);
        lines.clear();
        return false;
      } else {
        continue_even_though_large_file = true;
      }
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
    } else if (first) {
      first = false;
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

    std::mutex match_mutex;
    std::vector<std::pair<unsigned long long, unsigned long long>> matches{};
    std::atomic<size_t> number_of_matches = 0;
    file_context ctx{number_of_matches, matches, match_mutex,
                     options.print_only_filenames};

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
          filename.data(), buffer, search_size, ctx.matches,
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
        if (bytes_read > search_size) {
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
  }

  close(fd);

  if ((result || options.count_include_zeros) && options.count_matching_lines &&
      !options.print_only_filenames) {
    if (options.print_filenames) {
      if (options.is_stdout) {
        fmt::print("{}:{}\n",
                   fmt::format(fg(fmt::color::steel_blue), "{}", filename),
                   num_matching_lines);
      } else {
        fmt::print("{}:{}\n", filename, num_matching_lines);
      }
    } else {
      fmt::print("{}\n", num_matching_lines);
    }
  } else if ((result || options.count_include_zeros) && options.count_matches &&
             !options.print_only_filenames) {
    if (options.print_filenames) {
      if (options.is_stdout) {
        fmt::print("{}:{}\n",
                   fmt::format(fg(fmt::color::steel_blue), "{}", filename),
                   num_matches);
      } else {
        fmt::print("{}:{}\n", filename, num_matches);
      }
    } else {
      fmt::print("{}\n", num_matches);
    }
  } else {
    if (result && options.print_only_filenames) {
      if (options.is_stdout) {
        fmt::print(fg(fmt::color::steel_blue), "{}\n", filename);
      } else {
        fmt::print("{}\n", filename);
      }
    } else if (result && !options.count_matching_lines &&
               !options.count_matches && !options.print_only_filenames &&
               !lines.empty()) {
      if (options.is_stdout) {
        if (options.print_filenames) {
          lines = fmt::format(fg(fmt::color::steel_blue), "\n{}\n", filename) +
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

void directory_search::visit_directory_and_enqueue(
    moodycamel::ProducerToken &ptok, std::string directory,
    hs_scratch *local_file_filter_scratch) {
  DIR *dir = opendir(directory.c_str());
  if (dir == NULL) {
    std::cerr << "Failed to open '" << directory << "'\n";
    return;
  }

  struct dirent *entry;
  while (true) {
    entry = readdir(dir);
    if (!entry) {
      break;
    }

    // Ignore symlinks
    if (entry->d_type == DT_LNK) {
      continue;
    }

    // Ignore dot files/directories unless requested
    if (!options.search_hidden_files && entry->d_name[0] == '.')
      continue;

    // Check if the entry is a directory
    if (entry->d_type == DT_DIR) {
      // Enqueue subdirectory for processing
      const auto path = std::filesystem::path{directory} / entry->d_name;
      subdirectories.enqueue(std::move(path));
      num_dirs_enqueued += 1;
    } else if (entry->d_type == DT_REG) {
      const auto path = std::filesystem::path{directory} / entry->d_name;

      if (!options.filter_files ||
          (options.filter_files &&
           filter_file(path.c_str(), file_filter_database,
                       local_file_filter_scratch, options.negate_filter))) {
        if (options.perform_search) {
          queue.enqueue(ptok, std::move(path));
          ++num_files_enqueued;
        } else {
          if (options.is_stdout) {
            fmt::print(fg(fmt::color::steel_blue), "{}\n", path.c_str());
          } else {
            fmt::print("{}\n", path.c_str());
          }
        }
      }
    }
  }

  closedir(dir);
}

bool directory_search::try_dequeue_and_process_path(
    moodycamel::ConsumerToken &ctok, hs_scratch_t *local_scratch, char *buffer,
    std::string &lines) {
  constexpr std::size_t BULK_DEQUEUE_SIZE = 32;
  std::string entries[BULK_DEQUEUE_SIZE];
  auto count = queue.try_dequeue_bulk(ctok, entries, BULK_DEQUEUE_SIZE);
  if (count > 0) {
    for (std::size_t j = 0; j < count; ++j) {
      process_file(std::move(entries[j]), local_scratch, buffer, lines);
    }
    num_files_dequeued += count;
    return true;
  }

  return false;
}
#include <directory_search.hpp>
#include <is_binary.hpp>

directory_search::directory_search(std::string &pattern,
                                   const std::filesystem::path &path,
                                   argparse::ArgumentParser &program)
    : search_path(path) {
  options.search_binary_files = program.get<bool>("--text");
  options.count_matching_lines = program.get<bool>("-c");
  options.count_matches = program.get<bool>("--count-matches");
  compile_pattern_as_literal = program.get<bool>("-F");
  options.num_threads = program.get<unsigned>("-j");
  auto show_line_number = program.get<bool>("-n");
  auto hide_line_number = program.get<bool>("-N");
  options.exclude_submodules = program.get<bool>("--exclude-submodules");
  options.ignore_case = program.get<bool>("-i");
  options.count_include_zeros = program.get<bool>("--include-zero");
  options.print_filenames = !(program.get<bool>("-I"));
  options.print_only_filenames = program.get<bool>("-l");
  if (program.is_used("--filter")) {
    options.filter_file_pattern = program.get<std::string>("--filter");
    options.filter_files = true;
    if (!construct_file_filtering_hs_database()) {
      throw std::runtime_error("Error compiling pattern " +
                               options.filter_file_pattern);
    }
  }

  if (program.is_used("-M")) {
    options.max_column_limit = program.get<std::size_t>("-M");
  }

  if (program.is_used("--max-filesize")) {
    const auto max_file_size_spec = program.get<std::string>("--max-filesize");
    options.max_file_size = size_to_bytes(max_file_size_spec);
  }

  // Check if word boundary is requested
  if (program.get<bool>("-w")) {
    pattern = "\\b" + pattern + "\\b";
  }

  options.use_ucp = program.get<bool>("--ucp");
  options.search_hidden_files = program.get<bool>("--hidden");
  options.print_only_matching_parts = program.get<bool>("-o");

  options.is_stdout = isatty(STDOUT_FILENO) == 1;

  if (options.is_stdout) {
    // By default show line numbers
    // unless -N is used
    options.show_line_numbers = (!hide_line_number);
  } else {
    // By default hide line numbers
    // unless -n is used
    options.show_line_numbers = show_line_number;
  }

  options.show_column_numbers = program.get<bool>("--column");
  if (options.show_column_numbers) {
    options.show_line_numbers = true;
  }

  options.show_byte_offset = program.get<bool>("-b");

  perform_search = !program.get<bool>("--files");
  if (perform_search) {
    compile_hs_database(pattern);
  }
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
  fmt::print("{}/{} {}\n", num_files_dequeued.load(), num_files_enqueued.load(),
             num_large_files_enqueued.load());
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
  git_libgit2_init();

  std::vector<std::thread> consumer_threads(options.num_threads);
  if (perform_search) {
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
  if (perform_search) {
    search_thread_function();

    for (std::size_t i = 0; i < options.num_threads; ++i) {
      consumer_threads[i].join();
    }
  }

  // All threads are done processing the file queue

  // Search git repos one by one
  if (num_git_repos_enqueued > 0) {
    auto current_path = std::filesystem::current_path();

    while (num_git_repos_enqueued > 0) {
      std::string repo_path{};
      auto found = git_repo_paths.try_dequeue(repo_path);
      if (found) {

        git_index_search git_index_searcher(
            database, scratch, file_filter_database, file_filter_scratch,
            perform_search, options, repo_path);
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

  if (perform_search) {
    // Now search large files one by one using the large_file_searcher
    if (num_large_files_enqueued > 0) {
      file_search large_file_searcher(
          database, scratch,
          file_search_options{
              options.is_stdout, options.show_line_numbers,
              options.show_column_numbers, options.show_byte_offset,
              options.ignore_case, options.count_matching_lines,
              options.count_matches, options.count_include_zeros,
              options.use_ucp, options.num_threads, options.print_filenames,
              options.print_only_matching_parts, options.max_column_limit,
              options.print_only_filenames});

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

void directory_search::compile_hs_database(std::string &pattern) {
  hs_error_t error_code;
  hs_compile_error_t *compile_error = NULL;
  if (compile_pattern_as_literal) {
    error_code = hs_compile_lit(
        pattern.data(),
        (options.ignore_case ? HS_FLAG_CASELESS : 0) |
            (options.is_stdout || options.print_only_matching_parts ||
                     options.show_column_numbers || options.show_byte_offset
                 ? HS_FLAG_SOM_LEFTMOST
                 : 0),
        pattern.size(), HS_MODE_BLOCK, NULL, &database, &compile_error);
  } else {
    error_code = hs_compile(
        pattern.data(),
        (options.ignore_case ? HS_FLAG_CASELESS : 0) | HS_FLAG_UTF8 |
            (options.use_ucp ? HS_FLAG_UCP : 0) |
            (options.is_stdout || options.print_only_matching_parts ||
                     options.show_column_numbers || options.show_byte_offset
                 ? HS_FLAG_SOM_LEFTMOST
                 : 0),
        HS_MODE_BLOCK, NULL, &database, &compile_error);
  }
  if (error_code != HS_SUCCESS) {
    throw std::runtime_error(std::string{"Error compiling pattern: "} +
                             compile_error->message);
  }

  auto database_error = hs_alloc_scratch(database, &scratch);
  if (database_error != HS_SUCCESS) {
    throw std::runtime_error("Error allocating scratch space");
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

  if (!options.search_binary_files && is_blacklisted(filename)) {
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

    if (first && !options.search_binary_files) {
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

    // Find the position of the last newline in the buffer
    // In order to catch matches between chunks, need to amend the buffer
    // and make sure it stops at a new line boundary
    char *last_newline = (char *)memrchr(buffer, '\n', bytes_read);
    std::size_t search_size = bytes_read;
    if (last_newline) {
      search_size = last_newline - buffer;
    } else {
      // Not a single newline in the entire chunk
      // This could be some binary file or some minified JS file
      // Skip it
      result = false;
      break;
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
      num_matching_lines +=
          process_fn(filename.data(), buffer, search_size, ctx.matches,
                     current_line_number, lines, options.print_filenames,
                     options.is_stdout, options.show_line_numbers,
                     options.show_column_numbers, options.show_byte_offset,
                     options.print_only_matching_parts,
                     options.max_column_limit, total_bytes_read - bytes_read);
      num_matches += ctx.number_of_matches;
    }

    if (last_newline && bytes_read > search_size &&
        bytes_read == FILE_CHUNK_SIZE) /* Not the last chunk */ {

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
           filter_file(path.c_str(), local_file_filter_scratch))) {
        if (perform_search) {
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

bool directory_search::construct_file_filtering_hs_database() {
  hs_compile_error_t *compile_error = NULL;
  hs_error_t error_code =
      hs_compile(options.filter_file_pattern.c_str(), HS_FLAG_UTF8,
                 HS_MODE_BLOCK, NULL, &file_filter_database, &compile_error);
  if (error_code != HS_SUCCESS) {
    fprintf(stderr, "Error compiling pattern: %s\n", compile_error->message);
    hs_free_compile_error(compile_error);
    return false;
  }

  hs_error_t database_error =
      hs_alloc_scratch(file_filter_database, &file_filter_scratch);
  if (database_error != HS_SUCCESS) {
    fprintf(stderr, "Error allocating scratch space\n");
    hs_free_database(file_filter_database);
    return false;
  }

  return true;
}

int directory_search::on_file_filter_match(unsigned int id,
                                           unsigned long long from,
                                           unsigned long long to,
                                           unsigned int flags, void *ctx) {
  filter_context *fctx = (filter_context *)(ctx);
  fctx->result = true;
  return HS_SUCCESS;
}

bool directory_search::filter_file(const char *path,
                                   hs_scratch *local_file_filter_scratch) {
  filter_context ctx{false};
  if (hs_scan(file_filter_database, path, strlen(path), 0,
              local_file_filter_scratch, on_file_filter_match,
              (void *)(&ctx)) != HS_SUCCESS) {
    return true;
  }
  return ctx.result;
}

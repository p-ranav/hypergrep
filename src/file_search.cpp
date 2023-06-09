#include <hypergrep/file_search.hpp>

file_search::file_search(std::string &pattern,
                         argparse::ArgumentParser &program) {
  initialize_search(pattern, program, options, &database, &scratch,
                    &file_filter_database, &file_filter_scratch);

  if (!program.is_used("-j")) {
    options.num_threads += 1;
  }
}

file_search::file_search(hs_database_t *database, hs_scratch_t *scratch,
                         const search_options &options)
    : database(database), scratch(scratch), options(options) {
  non_owning_database = true;
}

file_search::~file_search() {
  if (!non_owning_database) {
    if (scratch) {
      hs_free_scratch(scratch);
    }
    if (database) {
      hs_free_database(database);
    }
  }

  for (const auto &s : thread_local_scratch) {
    hs_free_scratch(s);
  }
}

void file_search::run(std::filesystem::path path,
                      std::optional<std::size_t> maybe_file_size) {

  if (!options.perform_search) {
    if (options.is_stdout) {
      fmt::print(fg(fmt::color::steel_blue), "{}\n", path.c_str());
    } else {
      fmt::print("{}\n", path.c_str());
    }
    return;
  }

  if (!database) {
    throw std::runtime_error("Database is NULL");
  }
  // Set up the scratch space
  hs_scratch_t *local_scratch = NULL;
  hs_error_t database_error = hs_alloc_scratch(database, &local_scratch);
  if (database_error != HS_SUCCESS) {
    throw std::runtime_error("Error allocating scratch space");
  }
  thread_local_scratch.push_back(local_scratch);

  // Memory map and search file in chunks multithreaded
  mmap_and_scan(std::move(path), maybe_file_size);
}

struct chunk_result {
  char *start{nullptr};
  char *end{nullptr};
  std::vector<std::pair<unsigned long long, unsigned long long>> matches{};
  std::size_t line_count{0};
};

bool file_search::mmap_and_scan(std::string &&filename,
                                std::optional<std::size_t> maybe_file_size) {
  int fd = open(filename.data(), O_RDONLY, 0);
  if (fd == -1) {
    std::cerr << filename << ": " << std::strerror(errno) << " (os error " << errno << ")\n";
    return false;
  }

  // Get the size of the file
  std::size_t file_size{0};
  if (maybe_file_size.has_value()) {
    file_size = maybe_file_size.value();
  } else {
    struct stat sb;
    if (fstat(fd, &sb) == -1) {
      return false;
    }
    file_size = sb.st_size;
  }

  // Memory map the file
  char *buffer = (char *)mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (buffer == MAP_FAILED) {
    return false;
  }

  const auto process_fn =
      (options.is_stdout || options.print_only_matching_parts ||
       options.show_column_numbers || options.show_byte_offset)
          ? process_matches
          : process_matches_nocolor_nostdout;

  // Use the data

  // NOTE: hs_scan takes a `unsigned int` buffer size (2^32-1)
  // If the file_size is larger than that (e.g., 13GB), then
  // the search would need to be performed in chunks
  std::size_t max_searchable_size =
      FILE_CHUNK_SIZE; // std::numeric_limits<unsigned int>::max() - 1; // max
                       // that hs_scan can handle

  // Algorithm:
  // Spawn N-1 threads (N = max hardware concurrency)
  // Each thread will be given: buffer, file_size, offset
  // Each thread will search from the bytes {buffer + offset, buffer + offset +
  // search_size} Each thread will print its results

  auto max_concurrency = options.num_threads;
  if (max_concurrency > 1) {
    max_concurrency -= 1;
  }

  // Each thread will enqueue its results into its queue
  moodycamel::ConcurrentQueue<chunk_result> *output_queues =
      new moodycamel::ConcurrentQueue<chunk_result>[max_concurrency];

  std::vector<std::thread> threads(max_concurrency);
  thread_local_scratch.reserve(max_concurrency);

  std::atomic<std::size_t> num_threads_finished{0};
  std::atomic<std::size_t> num_results_enqueued{0}, num_results_dequeued{0};
  std::atomic<std::size_t> num_matches{0};
  std::atomic<bool> single_match_found{false};

  for (std::size_t i = 0; i < max_concurrency; ++i) {

    // Set up the scratch space
    hs_scratch_t *local_scratch = NULL;
    hs_error_t database_error = hs_alloc_scratch(database, &local_scratch);
    if (database_error != HS_SUCCESS) {
      fprintf(stderr, "Error allocating scratch space\n");
      hs_free_database(database);
      return false;
    }
    thread_local_scratch.push_back(local_scratch);

    // Spawn a reader thread
    threads[i] = std::thread(
        [this, i = i, max_concurrency = max_concurrency, buffer = buffer,
         file_size = file_size, max_searchable_size = max_searchable_size,
         &output_queues, &num_results_enqueued, &num_threads_finished,
         &single_match_found, &num_matches]() {
          // Set up the scratch space
          hs_scratch_t *local_scratch = thread_local_scratch[i];

          std::size_t offset{i * max_searchable_size};
          char *eof = buffer + file_size;

          while (true) {

            if (options.print_only_filenames && single_match_found) {
              break;
            }

            char *start = buffer + offset;
            if (start >= buffer + file_size) {
              // stop here
              num_threads_finished += 1;
              break;
            }

            if (offset > 0) {
              // Adjust start to go back to a newline boundary
              // Adjust end as well to go back to a newline boundary
              std::string_view chunk(buffer, start - buffer);

              auto last_newline = chunk.find_last_of('\n', start - buffer);
              if (last_newline == std::string_view::npos) {
                // No newline found, do nothing?
                // TODO: This could be an error scenario, check
              } else {
                start = buffer + last_newline;
              }
            }

            char *end = buffer + offset + max_searchable_size;
            if (end > eof) {
              end = eof;
            }

            // Update end to stop at a newline boundary
            std::string_view chunk(start, end - start);
            if (end != eof) {
              auto last_newline = chunk.find_last_of('\n', end - start);
              if (last_newline == std::string_view::npos) {
                // No newline found, do nothing?
                // TODO: This could be an error scenario, check
              } else {
                end = start + last_newline;
              }
            }

            // Perform the search
            std::vector<std::pair<unsigned long long, unsigned long long>>
                matches{};
            std::atomic<size_t> number_of_matches = 0;
            file_context ctx{number_of_matches, matches,
                             options.print_only_filenames};

            if (hs_scan(database, start, end - start, 0, local_scratch,
                        on_match, (void *)(&ctx)) != HS_SUCCESS) {
              if (options.print_only_filenames && ctx.number_of_matches > 0) {
                single_match_found = true;
              }
              num_threads_finished += 1;
              break;
            }

            num_matches += ctx.number_of_matches;

            // Save result
            std::size_t line_count_at_end_of_chunk =
                std::count(start, end, '\n');
            chunk_result local_chunk_result{start, end, std::move(matches),
                                            line_count_at_end_of_chunk};
            output_queues[i].enqueue(std::move(local_chunk_result));
            num_results_enqueued += 1;

            offset += max_concurrency * max_searchable_size;
          }
        });
  }

  std::size_t num_matching_lines{0};
  bool filename_printed{false};
  std::size_t current_line_number = 1;
  std::size_t i = 0;

  if (!options.print_only_filenames) {
    // In this main thread
    // Dequeue from output_queues, process matches
    // and print output
    while (!(num_threads_finished == max_concurrency &&
             num_results_enqueued == num_results_dequeued)) {
      chunk_result next_result{};

      auto found = output_queues[i].try_dequeue(next_result);
      if (found) {

        // Do something with result
        // Print it
        std::string lines{};
        auto start = next_result.start;
        auto end = next_result.end;
        auto &matches = next_result.matches;
        if (!matches.empty()) {
          std::size_t previous_line_number = current_line_number;
          num_matching_lines += process_fn(
              filename.data(), start, end - start, next_result.matches,
              previous_line_number, lines, options.print_filenames,
              options.is_stdout, options.show_line_numbers,
              options.show_column_numbers, options.show_byte_offset,
              options.print_only_matching_parts, options.max_column_limit,
              (start - buffer), options.ltrim_each_output_line);

          if (!options.count_matching_lines && !options.count_matches &&
              !options.print_only_filenames && !lines.empty()) {

            if (options.print_filenames && !filename_printed) {
              if (options.is_stdout) {
                fmt::print(fg(fmt::color::steel_blue), "{}\n", filename);
              }
              filename_printed = true;
            }

            fmt::print("{}", lines);
          }
        }
        current_line_number += next_result.line_count;

        num_results_dequeued += 1;

        i += 1;
        if (i == max_concurrency) {
          i = 0;
        }
      }
    }
  }

  for (auto &t : threads) {
    t.join();
  }

  if ((num_matching_lines > 0 || options.count_include_zeros) &&
      options.count_matching_lines && !options.print_only_filenames) {
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
  } else if ((num_matches.load() > 0 || options.count_include_zeros) &&
             options.count_matches && !options.print_only_filenames) {
    if (options.print_filenames) {
      if (options.is_stdout) {
        fmt::print("{}:{}\n",
                   fmt::format(fg(fmt::color::steel_blue), "{}", filename),
                   num_matches.load());
      } else {
        fmt::print("{}:{}\n", filename, num_matches.load());
      }
    } else {
      fmt::print("{}\n", num_matches.load());
    }
  } else if (options.print_only_filenames && single_match_found) {
    if (options.is_stdout) {
      fmt::print(fg(fmt::color::steel_blue), "{}\n", filename);
    } else {
      fmt::print("{}\n", filename);
    }
  }

  // Unmap the file
  if (munmap(buffer, file_size) == -1) {
    return false;
  }

  // Close the file
  if (close(fd) == -1) {
    return false;
  }

  delete[] output_queues;

  if (options.is_stdout && num_matching_lines > 0) {
    fmt::print("\n");
  }

  return true;
}

bool file_search::scan_line(std::string &line, std::size_t &current_line_number,
                            bool &break_loop) {
  static hs_scratch_t *local_scratch = NULL;
  static bool scratch_allocated = [this]() -> bool {
    if (!database) {
      throw std::runtime_error("Database is NULL");
    }
    // Set up the scratch space
    hs_error_t database_error = hs_alloc_scratch(database, &local_scratch);
    if (database_error != HS_SUCCESS) {
      throw std::runtime_error("Error allocating scratch space");
    }
    thread_local_scratch.push_back(local_scratch);

    return true;
  }();

  if (!scratch_allocated) {
    return false;
  }

  const auto process_fn =
      (options.is_stdout || options.print_only_matching_parts ||
       options.show_column_numbers || options.show_byte_offset)
          ? process_matches
          : process_matches_nocolor_nostdout;

  // Perform the search
  bool result{false};
  std::vector<std::pair<unsigned long long, unsigned long long>> matches{};
  std::atomic<size_t> number_of_matches = 0;
  file_context ctx{number_of_matches, matches, options.print_only_filenames};

  if (hs_scan(database, line.data(), line.size(), 0, local_scratch, on_match,
              (void *)(&ctx)) != HS_SUCCESS) {
    if (options.print_only_filenames && ctx.number_of_matches > 0) {
      break_loop = true;
    }
    result = false;
  } else {
    if (ctx.number_of_matches > 0) {
      result = true;
    }
  }

  // Process matches with this line number as the start line number
  // (for this chunk)
  if (ctx.number_of_matches > 0) {
    std::string lines{};
    const std::string filename{""};
    process_fn(filename.data(), line.data(), line.size(), ctx.matches,
               current_line_number, lines, false, options.is_stdout,
               options.show_line_numbers, options.show_column_numbers,
               options.show_byte_offset, options.print_only_matching_parts,
               options.max_column_limit, 0, options.ltrim_each_output_line);

    if (!options.count_matching_lines && !options.print_only_filenames &&
        result && !lines.empty()) {
      fmt::print("{}", lines);
    } else if (options.print_only_filenames) {
      if (options.is_stdout) {
        fmt::print(fg(fmt::color::steel_blue), "<stdin>\n");
      } else {
        fmt::print("<stdin>\n");
      }
    }
  }

  current_line_number += 1;

  return result;
}

#include <file_search.hpp>

file_search::file_search(hs_database_t *database, hs_scratch_t *scratch,
                         const file_search_options &options)
    : database(database), scratch(scratch), options(options) {
  non_owning_database = true;
}

file_search::file_search(argparse::ArgumentParser &program) {
  options.count_matching_lines = program.get<bool>("-c");
  options.num_threads = program.get<unsigned>("-j");
  auto show_line_number = program.get<bool>("-n");
  auto hide_line_number = program.get<bool>("-N");
  options.ignore_case = program.get<bool>("-i");
  auto pattern = program.get<std::string>("pattern");

  // Check if word boundary is requested
  if (program.get<bool>("-w")) {
    pattern = "\\b" + pattern + "\\b";
  }

  options.use_ucp = program.get<bool>("--ucp");

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

  compile_hs_database(pattern);
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

void file_search::compile_hs_database(std::string &pattern) {
  hs_compile_error_t *compile_error = NULL;
  auto error_code =
      hs_compile(pattern.data(),
                 (options.ignore_case ? HS_FLAG_CASELESS : 0) | HS_FLAG_UTF8 |
                     (options.use_ucp ? HS_FLAG_UCP : 0) | HS_FLAG_SOM_LEFTMOST,
                 HS_MODE_BLOCK, NULL, &database, &compile_error);
  if (error_code != HS_SUCCESS) {
    throw std::runtime_error(std::string{"Error compiling pattern: "} +
                             compile_error->message);
  }

  auto database_error = hs_alloc_scratch(database, &scratch);
  if (database_error != HS_SUCCESS) {
    throw std::runtime_error("Error allocating scratch space");
  }
}

void file_search::run(std::filesystem::path path) {
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
  mmap_and_scan(std::move(path));
}

struct chunk_result {
  char* start{nullptr};
  char* end{nullptr};
  std::vector<std::pair<unsigned long long, unsigned long long>> matches{};
};

bool file_search::mmap_and_scan(std::string &&filename) {
  int fd = open(filename.data(), O_RDONLY, 0);
  if (fd == -1) {
    return false;
  }

  // Get the size of the file
  struct stat sb;
  if (fstat(fd, &sb) == -1) {
    return false;
  }
  std::size_t file_size = sb.st_size;

  // Memory map the file
  char *buffer = (char *)mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (buffer == MAP_FAILED) {
    return false;
  }

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
  moodycamel::ConcurrentQueue<chunk_result>
      *output_queues =
          new moodycamel::ConcurrentQueue<chunk_result>[max_concurrency];

  std::vector<std::thread> threads(max_concurrency);
  thread_local_scratch.reserve(max_concurrency);

  std::atomic<std::size_t> num_threads_finished{0};
  std::atomic<std::size_t> num_results_enqueued{0}, num_results_dequeued{0};

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
    threads[i] = std::thread([this, i = i, max_concurrency = max_concurrency,
                              buffer = buffer, file_size = file_size,
                              max_searchable_size = max_searchable_size,
                              &output_queues,
                              &num_results_enqueued,
                              &num_threads_finished]() {

      // Set up the scratch space
      hs_scratch_t *local_scratch = thread_local_scratch[i];

      std::size_t offset{i * max_searchable_size};
      char *eof = buffer + file_size;

      while (true) {

        char *start = buffer + offset;
        if (start > buffer + file_size) {
          // stop here
          num_threads_finished += 1;
          break;
        }

        char *end = start + max_searchable_size;
        if (end > buffer + file_size) {
          end = buffer + file_size;
        }

        if (offset > 0) {
          // Adjust start to go back to a newline boundary
          // Adjust end as well to go back to a newline boundary
          auto previous_start = start - max_searchable_size;
          if (previous_start > buffer) {
            std::string_view chunk(previous_start, max_searchable_size);

            auto last_newline = chunk.find_last_of('\n', max_searchable_size);
            if (last_newline == std::string_view::npos) {
              // No newline found, do nothing?
              // TODO: This could be an error scenario, check
            } else {
              start = previous_start + last_newline;
            }
          } else {
            // Something wrong, don't update start
          }
        }

        // Update end to stop at a newline boundary
        std::string_view chunk(start, end - start);
        auto last_newline = chunk.find_last_of('\n', end - start);
        if (last_newline == std::string_view::npos) {
          // No newline found, do nothing?
          // TODO: This could be an error scenario, check
        } else {
          end = start + last_newline;
          if (offset == 0) {
            end += 1;
          }
        }

        if (static_cast<std::size_t>(eof - end) < max_searchable_size) {
          // If "end" and EOF are separated by less than
          // a single chunk
          // just set end to the EOF
          end = eof;
        }

        // Perform the search
        std::mutex match_mutex;
        std::vector<std::pair<unsigned long long, unsigned long long>>
            matches{};
        std::atomic<size_t> number_of_matches = 0;
        file_context
	      ctx{number_of_matches, matches, match_mutex, false /* print_only_filenames is not relevant in a single file search */};

        if (hs_scan(database, start, end - start, 0, local_scratch, on_match,
                    (void *)(&ctx)) != HS_SUCCESS) {
          num_threads_finished += 1;
          break;
        }

        // Save result
        chunk_result local_chunk_result { start, end, std::move(matches) };
        output_queues[i].enqueue(std::move(local_chunk_result));
        num_results_enqueued += 1;

        offset += max_concurrency * max_searchable_size;
      }

    });

  }

  // In this main thread
  // Dequeue from output_queues, process matches
  // and print output
  std::size_t num_matching_lines{0};
  bool filename_printed{false};
  std::size_t previous_line_count = 1;
  std::size_t i = 0;
  while (!(num_threads_finished == max_concurrency && num_results_enqueued == num_results_dequeued)) {
    chunk_result next_result{};

    auto found = output_queues[i].try_dequeue(next_result);
    if (found) {

      // Do something with result
      // Print it
      std::string lines{};
      auto start = next_result.start;
      auto end = next_result.end;
      auto& matches = next_result.matches;
      if (!matches.empty()) {
        num_matching_lines += process_matches(filename.data(), start, end - start, next_result.matches,
          previous_line_count, lines, options.print_filename,
          options.is_stdout, options.show_line_numbers);

        if (!options.count_matching_lines && !lines.empty()) {

          if (options.print_filename && !filename_printed) {
            if (options.is_stdout) {
              fmt::print(fg(fmt::color::steel_blue), "{}\n", filename);
            }
            filename_printed = true;
          }

          fmt::print("{}", lines);
        }
      } else {
        // No matches, just count lines
        previous_line_count += std::count(start, end, '\n');
      }

      num_results_dequeued += 1;

      i += 1;
      if (i == max_concurrency) {
        i = 0;
      }
    }
  }

  for (auto &t : threads) {
    t.join();
  }

  if (options.count_matching_lines) {
    if (options.is_stdout) {
      fmt::print("{}:{}\n",
                 fmt::format(fg(fmt::color::steel_blue), "{}", filename),
                 num_matching_lines);
    } else {
      fmt::print("{}:{}\n", filename, num_matching_lines);
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

  return true;
}

#if 0
bool file_search::mmap_and_scan(std::string &&filename) {

  int fd = open(filename.data(), O_RDONLY, 0);
  if (fd == -1) {
    return false;
  }

  // Get the size of the file
  struct stat sb;
  if (fstat(fd, &sb) == -1) {
    return false;
  }
  std::size_t file_size = sb.st_size;

  // Memory map the file
  char *buffer = (char *)mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (buffer == MAP_FAILED) {
    return false;
  }

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

  // 1 queue between 2 adjacent threads (handling 2 adjacent chunks)
  // Thread 0 will enqueue the line count into the 0-1_Queue
  // Thread 1 will check this queue to see what line count to start from for
  // printing the output Thread 1 will similarly enqueue its line count into the
  // 1-2_Queue Thread 2 will check this 1-2_Queue to see what line count to
  // start from for printing the output
  // .
  // .
  // .
  // Thread 0 will check Thread N-0_Queue
  moodycamel::ConcurrentQueue<std::size_t>
      *inter_thread_synchronization_line_count_queue =
          new moodycamel::ConcurrentQueue<std::size_t>[max_concurrency];

  std::vector<std::thread> threads(max_concurrency);
  thread_local_scratch.reserve(max_concurrency);
  std::atomic<size_t> num_matching_lines{0};

  std::atomic<bool> filename_printed{false};

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
    threads[i] = std::thread([this, i = i, max_concurrency = max_concurrency,
                              buffer = buffer, file_size = file_size,
                              max_searchable_size = max_searchable_size,
                              &inter_thread_synchronization_line_count_queue,
                              &filename, &num_matching_lines,
                              &filename_printed]() {
      // Set up the scratch space
      hs_scratch_t *local_scratch = thread_local_scratch[i];

      std::size_t offset{i * max_searchable_size};
      char *eof = buffer + file_size;
      while (true) {

        char *start = buffer + offset;
        if (start > buffer + file_size) {
          // stop here
          break;
        }

        char *end = start + max_searchable_size;
        if (end > buffer + file_size) {
          end = buffer + file_size;
        }

        if (offset > 0) {
          // Adjust start to go back to a newline boundary
          // Adjust end as well to go back to a newline boundary
          auto previous_start = start - max_searchable_size;
          if (previous_start > buffer) {
            std::string_view chunk(previous_start, max_searchable_size);

            auto last_newline = chunk.find_last_of('\n', max_searchable_size);
            if (last_newline == std::string_view::npos) {
              // No newline found, do nothing?
              // TODO: This could be an error scenario, check
            } else {
              start = previous_start + last_newline;
            }
          } else {
            // Something wrong, don't update start
          }
        }

        // Update end to stop at a newline boundary
        std::string_view chunk(start, end - start);
        auto last_newline = chunk.find_last_of('\n', end - start);
        if (last_newline == std::string_view::npos) {
          // No newline found, do nothing?
          // TODO: This could be an error scenario, check
        } else {
          end = start + last_newline;
          if (offset == 0) {
            end += 1;
          }
        }

        if (static_cast<std::size_t>(eof - end) < max_searchable_size) {
          // If "end" and EOF are separated by less than
          // a single chunk
          // just set end to the EOF
          end = eof;
        }

        // Perform the search
        bool result{false};
        std::mutex match_mutex;
        std::vector<std::pair<unsigned long long, unsigned long long>>
            matches{};
        std::atomic<size_t> number_of_matches = 0;
        file_context
	      ctx{number_of_matches, matches, match_mutex, false /* print_only_filenames is not relevant in a single file search */};

        if (hs_scan(database, start, end - start, 0, local_scratch, on_match,
                    (void *)(&ctx)) != HS_SUCCESS) {
          result = false;
          break;
        } else {
          if (ctx.number_of_matches > 0) {
            result = true;
          }
        }

        // Find the line count on the previous chunk
        std::size_t previous_line_count{0};
        if (offset == 0) {
          previous_line_count = 1;
        }
        if (/*options.show_line_numbers && */ offset > 0) {
          // If not the first chunk,
          // get the number of lines (computed) in the previous chunk
          auto thread_number = (i > 0) ? i - 1 : max_concurrency - 1;

          bool found = false;
          while (!found) {
            found = inter_thread_synchronization_line_count_queue[thread_number]
                        .try_dequeue(previous_line_count);
          }
        }

        const std::size_t line_count_at_start_of_chunk = previous_line_count;

        // Process matches with this line number as the start line number
        // (for this chunk)
        if (ctx.number_of_matches > 0) {
          std::string lines{};
          process_matches(filename.data(), start, end - start, ctx,
                          previous_line_count, lines, options.print_filename,
                          options.is_stdout, options.show_line_numbers);
          num_matching_lines += ctx.number_of_matches;

          if (!options.count_matching_lines && result && !lines.empty()) {

            if (options.print_filename && !filename_printed) {
              if (options.is_stdout) {
                fmt::print(fg(fmt::color::steel_blue), "{}\n", filename);
              }
              filename_printed = true;
            }

            fmt::print("{}", lines);
          }
        }

        // if (options.show_line_numbers) {
        // Count num lines in the chunk that was just searched
        const std::size_t num_lines_in_chunk = std::count(start, end, '\n');
        inter_thread_synchronization_line_count_queue[i].enqueue(
            line_count_at_start_of_chunk + num_lines_in_chunk);
        // }

        offset += max_concurrency * max_searchable_size;
      }

      return true;
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  if (options.count_matching_lines) {
    if (options.is_stdout) {
      fmt::print("{}:{}\n",
                 fmt::format(fg(fmt::color::steel_blue), "{}", filename),
                 num_matching_lines);
    } else {
      fmt::print("{}:{}\n", filename, num_matching_lines);
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

  delete[] inter_thread_synchronization_line_count_queue;

  return true;
}
#endif

bool file_search::scan_line(std::string &line,
                            std::size_t &current_line_number) {
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

  // Perform the search
  bool result{false};
  std::mutex match_mutex;
  std::vector<std::pair<unsigned long long, unsigned long long>> matches{};
  std::atomic<size_t> number_of_matches = 0;
  file_context ctx{
      number_of_matches, matches, match_mutex,
      false /* print_only_filenames is not relevant in a single file search */};

  if (hs_scan(database, line.data(), line.size(), 0, local_scratch, on_match,
              (void *)(&ctx)) != HS_SUCCESS) {
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
    process_matches(filename.data(), line.data(), line.size(), ctx.matches,
                    current_line_number, lines, false, options.is_stdout,
                    options.show_line_numbers);

    if (!options.count_matching_lines && result && !lines.empty()) {
      fmt::print("{}", lines);
    }
  }

  current_line_number += 1;

  return result;
}
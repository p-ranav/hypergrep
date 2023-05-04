#include "concurrentqueue.h"
#include <argparse/argparse.hpp>
#include <atomic>
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <fmt/color.h>
#include <fmt/format.h>
#include <fstream>
#include <git2.h>
#include <hs/hs.h>
#include <limits>
#include <numeric>
#include <set>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

inline bool is_elf_header(const char *buffer) {
  static constexpr std::string_view elf_magic = "\x7f"
                                                "ELF";
  return (strncmp(buffer, elf_magic.data(), elf_magic.size()) == 0);
}

inline bool is_archive_header(const char *buffer) {
  static constexpr std::string_view archive_magic = "!<arch>";
  return (strncmp(buffer, archive_magic.data(), archive_magic.size()) == 0);
}

moodycamel::ConcurrentQueue<std::string> queue;
moodycamel::ProducerToken ptok(queue);

bool is_stdout{true};

std::atomic<bool> running{true};
std::atomic<std::size_t> num_files_enqueued{0};
std::atomic<std::size_t> num_files_dequeued{0};

constexpr std::size_t TYPICAL_FILESYSTEM_BLOCK_SIZE = 4096;
constexpr std::size_t FILE_CHUNK_SIZE = 16 * TYPICAL_FILESYSTEM_BLOCK_SIZE;

hs_database_t *database = NULL;
hs_scratch_t *scratch = NULL;
std::vector<hs_scratch *> thread_local_scratch;
std::vector<hs_scratch *> thread_local_scratch_per_line;

bool option_show_line_numbers{false};
bool option_ignore_case{false};
bool option_print_only_filenames{false};
bool option_count_matching_lines{false};

bool option_filter_files{false};
std::string option_filter_file_pattern{};
hs_database_t *file_filter_database = NULL;
hs_scratch_t *file_filter_scratch = NULL;

struct filter_context {
  bool result{false};
};

static int on_file_filter_match(unsigned int id, unsigned long long from,
                                unsigned long long to, unsigned int flags,
                                void *ctx) {
  filter_context *fctx = (filter_context *)(ctx);
  fctx->result = true;
  return HS_SUCCESS;
}

bool filter_file(const char *path) {
  filter_context ctx{false};
  if (hs_scan(file_filter_database, path, strlen(path), 0, file_filter_scratch,
              on_file_filter_match, (void *)(&ctx)) != HS_SUCCESS) {
    return true;
  }
  return ctx.result;
}

struct file_context {
  std::atomic<size_t> &number_of_matches;
  std::set<std::pair<unsigned long long, unsigned long long>> &matches;
  hs_scratch *local_scratch;
};

static int on_match(unsigned int id, unsigned long long from,
                    unsigned long long to, unsigned int flags, void *ctx) {
  file_context *fctx = (file_context *)(ctx);
  fctx->number_of_matches += 1;
  fctx->matches.insert(std::make_pair(from, to));

  if (option_print_only_filenames) {
    return HS_SCAN_TERMINATED;
  } else {
    return HS_SUCCESS;
  }
}

struct line_context {
  const char *data;
  std::string &lines;
  const char **current_ptr;
};

static int print_match_in_red_color(unsigned int id, unsigned long long from,
                                    unsigned long long to, unsigned int flags,
                                    void *ctx) {
  auto *fctx = (line_context *)(ctx);
  const char *line_data = fctx->data;
  auto &lines = fctx->lines;
  const char *start = *(fctx->current_ptr);
  lines += fmt::format("{}", std::string_view(start, line_data + from - start));
  lines += fmt::format(fg(fmt::color::red), "{}",
                       std::string_view(&line_data[from], to - from));
  *(fctx->current_ptr) = line_data + to;
  return 0;
}

void process_matches(const char *filename, char *buffer, std::size_t bytes_read,
                     file_context &ctx, std::size_t &current_line_number,
                     std::string &lines, bool print_filename = true) {
  std::string_view chunk(buffer, bytes_read);
  const char *start = buffer;
  const char *ptr = buffer;
  for (const auto &match : ctx.matches) {
    const auto previous_line_number = current_line_number;
    auto line_count = std::count(ptr, start + match.first, '\n');
    current_line_number += line_count;

    if (current_line_number == previous_line_number &&
        previous_line_number > 0) {
      // Ignore match, it's in the same line as the previous match
      continue;
    }

    ptr = start + match.second;

    auto end_of_line = chunk.find_first_of('\n', match.second);
    if (end_of_line == std::string_view::npos) {
      end_of_line = bytes_read;
    }

    auto start_of_line = chunk.find_last_of('\n', match.first);
    if (start_of_line == std::string_view::npos) {
      start_of_line = 0;
    } else {
      start_of_line += 1;
    }

    std::string_view line(start + start_of_line, end_of_line - start_of_line);
    if (is_stdout) {
      if (option_show_line_numbers) {
        lines += fmt::format(fg(fmt::color::green), "{}:", current_line_number);
      }

      const char *line_ptr = line.data();

      line_context nested_ctx{line_ptr, lines, &line_ptr};
      if (hs_scan(database, &buffer[start_of_line], end_of_line - start_of_line,
                  0, ctx.local_scratch, print_match_in_red_color,
                  &nested_ctx) != HS_SUCCESS) {
        return;
      }

      if (line_ptr != (&buffer[start_of_line] + end_of_line - start_of_line)) {
        // some left over
        lines += std::string(line_ptr, &buffer[start_of_line] + end_of_line -
                                           start_of_line - line_ptr);
      }

      lines += '\n';
    } else {
      if (print_filename) {
	if (option_show_line_numbers) {
	  lines += fmt::format("{}:{}:{}\n", filename, current_line_number,
			       std::string_view(&buffer[start_of_line],
						end_of_line - start_of_line));
	} else {
	  lines += fmt::format("{}:{}\n", filename,
			       std::string_view(&buffer[start_of_line],
						end_of_line - start_of_line));
	}
      }
      else {
	if (option_show_line_numbers) {
	  lines += fmt::format("{}:{}\n", current_line_number,
			       std::string_view(&buffer[start_of_line],
						end_of_line - start_of_line));
	} else {
	  lines += fmt::format("{}\n", std::string_view(&buffer[start_of_line],
							end_of_line - start_of_line));
	}
      }
    }
  }
}

bool process_file(std::string &&filename, std::size_t i, char *buffer,
                  std::string &lines) {
  int fd = open(filename.data(), O_RDONLY, 0);
  if (fd == -1) {
    return false;
  }
  bool result{false};

  // Set up the scratch space
  hs_scratch_t *local_scratch = thread_local_scratch[i];
  hs_scratch_t *local_scratch_per_line = thread_local_scratch_per_line[i];

  // Process the file in chunks
  std::size_t bytes_read = 0;
  std::atomic<std::size_t> max_line_number{0};
  std::size_t current_line_number{1};

  std::size_t num_matching_lines{0};

  // Read the file in chunks and perform search
  bool first{true};
  while ((bytes_read = read(fd, buffer, FILE_CHUNK_SIZE)) > 0) {
    if (first) {
      first = false;
      if (bytes_read >= 4 &&
          (is_elf_header(buffer) || is_archive_header(buffer))) {
        result = false;
        break;
      }

      if (memchr((void *)buffer, '\0', bytes_read) != NULL) {
        // NULL bytes found
        // Ignore file
        // Could be a .exe, .gz, .bin etc.
        result = false;
        break;
      }
    }

    // Find the position of the last newline in the buffer
    // In order to catch matches between chunks, need to amend the buffer
    // and make sure it stops at a new line boundary
    char *last_newline = (char *)memrchr(buffer, '\n', bytes_read);
    std::size_t search_size = bytes_read;
    if (last_newline) {
      search_size = last_newline - buffer;
    }

    std::set<std::pair<unsigned long long, unsigned long long>> matches{};
    std::atomic<size_t> number_of_matches = 0;
    file_context ctx{number_of_matches, matches, local_scratch_per_line};

    if (hs_scan(database, buffer, search_size, 0, local_scratch, on_match,
                (void *)(&ctx)) != HS_SUCCESS) {
      if (option_print_only_filenames && ctx.number_of_matches > 0) {
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
      process_matches(filename.data(), buffer, search_size, ctx,
                      current_line_number, lines);
      num_matching_lines += ctx.number_of_matches;
    }

    if (last_newline && bytes_read > search_size &&
        bytes_read == FILE_CHUNK_SIZE) /* Not the last chunk */ {

      if ((last_newline - buffer) == bytes_read - 1) {
        // Chunk ends exactly at the newline
        // Do nothing

        // TODO
        // There are probably other conditions too where lseek can be avoided
        // Research this
      } else {
        // Backtrack "remainder" number of characters
        // auto start = std::chrono::high_resolution_clock::now();
        lseek(fd, -1 * (bytes_read - search_size), SEEK_CUR);
        // auto end = std::chrono::high_resolution_clock::now();
        // fmt::print("{}us\n",
        // (std::chrono::duration_cast<std::chrono::microseconds>(end -
        // start).count()));
      }
    }
  }

  close(fd);

  if (result && option_count_matching_lines) {
    if (is_stdout) {
      fmt::print("{}:{}\n",
                 fmt::format(fg(fmt::color::steel_blue), "{}", filename),
                 num_matching_lines);
    } else {
      fmt::print("{}:{}\n", filename, num_matching_lines);
    }
  } else {
    if (result && option_print_only_filenames) {
      if (is_stdout) {
        fmt::print(fg(fmt::color::steel_blue), "{}\n", filename);
      } else {
        fmt::print("{}\n", filename);
      }
    } else if (result && !lines.empty()) {
      if (is_stdout) {
        lines =
            fmt::format(fg(fmt::color::steel_blue), "\n{}\n", filename) + lines;
        fmt::print("{}", lines);
      } else {
        fmt::print("{}", lines);
      }
    }
  }

  lines.clear();
  return result;
}

struct mmap_context {
  char *start;
  std::size_t size;
};

static int on_match_in_mmap(unsigned int id, unsigned long long from,
                            unsigned long long to, unsigned int flags,
                            void *ctx) {
  mmap_context *fctx = (mmap_context *)(ctx);
  return HS_SUCCESS;
}

bool process_file_mmap(std::string &&filename) {

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

  auto max_concurrency = std::thread::hardware_concurrency() - 1;
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
      inter_thread_synchronization_line_count_queue[max_concurrency];

  std::vector<std::thread> threads(max_concurrency);
  thread_local_scratch.reserve(max_concurrency);
  thread_local_scratch_per_line.reserve(max_concurrency);
  std::atomic<size_t> num_matching_lines{0};

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

    // Set up the scratch space per line
    hs_scratch_t *scratch_per_line = NULL;
    database_error = hs_alloc_scratch(database, &scratch_per_line);
    if (database_error != HS_SUCCESS) {
      fprintf(stderr, "Error allocating scratch space\n");
      hs_free_database(database);
      return false;
    }
    thread_local_scratch_per_line.push_back(scratch_per_line);

    // Spawn a reader thread
    threads[i] = std::thread([i = i, max_concurrency = max_concurrency,
                              buffer = buffer, file_size = file_size,
                              max_searchable_size = max_searchable_size,
                              &inter_thread_synchronization_line_count_queue,
                              &filename, &num_matching_lines]() {
      // Set up the scratch space
      hs_scratch_t *local_scratch = thread_local_scratch[i];
      hs_scratch_t *local_scratch_per_line = thread_local_scratch_per_line[i];

      std::size_t offset{i * max_searchable_size};
      while (true) {

        char *start = buffer + offset;
        if (start > buffer + file_size) {
          // stop here
          // fmt::print("Thread {} done\n", i);
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
        }

        // Perform the search
        bool result{false};
        std::set<std::pair<unsigned long long, unsigned long long>> matches{};
        std::atomic<size_t> number_of_matches = 0;
        file_context ctx{number_of_matches, matches, local_scratch_per_line};

        if (hs_scan(database, start, end - start, 0, local_scratch, on_match,
                    (void *)(&ctx)) != HS_SUCCESS) {
          if (option_print_only_filenames && ctx.number_of_matches > 0) {
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

        std::size_t previous_line_count{0};
	if (offset == 0) {
	  previous_line_count = 1;
	}
        if (option_show_line_numbers && offset > 0) {
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

        // Process matches with this line number as the start line number (for
        // this chunk)
        if (ctx.number_of_matches > 0) {
          std::string lines{};
          process_matches(filename.data(), start, end - start, ctx,
                          previous_line_count, lines, false);
          num_matching_lines += ctx.number_of_matches;

          if (!option_count_matching_lines && result && !lines.empty()) {
            fmt::print("{}", lines);
          }
        }

        if (option_show_line_numbers) {
          // Count num lines in the chunk that was just searched
          const std::size_t num_lines_in_chunk = std::count(start, end, '\n');
          inter_thread_synchronization_line_count_queue[i].enqueue(
              line_count_at_start_of_chunk + num_lines_in_chunk);
        }

        offset += max_concurrency * max_searchable_size;
      }

      return true;
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  if (option_count_matching_lines) {
    if (is_stdout) {
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

  return true;
}

bool visit_git_repo(const std::filesystem::path &dir,
                    git_repository *repo = nullptr);

bool search_submodules(const char *dir, git_repository *this_repo) {
  if (git_submodule_foreach(
          this_repo,
          [](git_submodule *sm, const char *name, void *payload) -> int {
            const char *dir = (const char *)(payload);
            auto path = std::filesystem::path(dir);

            git_repository *sm_repo = nullptr;
            if (git_submodule_open(&sm_repo, sm) == 0) {
              auto submodule_path = path / name;
              visit_git_repo(std::move(submodule_path), sm_repo);
            }
            return 0;
          },
          (void *)dir) != 0) {
    return false;
  } else {
    return true;
  }
}

bool visit_git_index(const std::filesystem::path &dir, git_index *index) {
  git_index_iterator *iter = nullptr;
  if (git_index_iterator_new(&iter, index) == 0) {

    const git_index_entry *entry = nullptr;
    while (git_index_iterator_next(&entry, iter) != GIT_ITEROVER) {
      if (entry && (!option_filter_files ||
                    (option_filter_files && filter_file(entry->path)))) {
        const auto path = dir / entry->path;
        queue.enqueue(ptok, path.string());
        ++num_files_enqueued;
      }
    }
    git_index_iterator_free(iter);
    return true;
  } else {
    return false;
  }
}

bool visit_git_repo(const std::filesystem::path &dir, git_repository *repo) {

  bool result{true};

  if (!repo) {
    // Open the repository
    if (git_repository_open(&repo, dir.c_str()) != 0) {
      result = false;
    }
  }

  // Load the git index for this repository
  git_index *index = nullptr;
  if (result && git_repository_index(&index, repo) != 0) {
    result = false;
  }

  // Visit each entry in the index
  if (result && !visit_git_index(dir, index)) {
    result = false;
  }

  // Search the submodules inside this submodule
  if (result && !search_submodules(dir.c_str(), repo)) {
    result = false;
  }

  git_index_free(index);
  git_repository_free(repo);
  return result;
}

void visit(const std::filesystem::path &path) {
  for (auto it = std::filesystem::recursive_directory_iterator(
           path, std::filesystem::directory_options::skip_permission_denied);
       it != std::filesystem::recursive_directory_iterator(); ++it) {
    const auto &path = it->path();
    const auto &filename = path.filename();
    const auto filename_cstr = filename.c_str();

    if (it->is_regular_file() && !it->is_symlink()) {

      if (filename_cstr[0] == '.')
        continue;

      if (!option_filter_files ||
          (option_filter_files && filter_file(path.c_str()))) {
        queue.enqueue(ptok, path.native());
        ++num_files_enqueued;
      }
    } else if (it->is_directory()) {
      if (filename_cstr[0] == '.' || it->is_symlink()) {
        // Stop processing this directory and its contents
        it.disable_recursion_pending();
      } else if (std::filesystem::exists(path / ".git")) {
        // Search this path as a git repo
        visit_git_repo(path, nullptr);

        // Stop processing this directory and its contents
        it.disable_recursion_pending();
      }
    }
  }
}

static inline bool visit_one(const std::size_t i, char *buffer,
                             std::string &lines) {
  constexpr std::size_t BULK_DEQUEUE_SIZE = 32;
  std::string entries[BULK_DEQUEUE_SIZE];
  auto count =
      queue.try_dequeue_bulk_from_producer(ptok, entries, BULK_DEQUEUE_SIZE);
  if (count > 0) {
    for (std::size_t j = 0; j < count; ++j) {
      process_file(std::move(entries[j]), i, buffer, lines);
    }
    num_files_dequeued += count;
    return true;
  }

  return false;
}

bool construct_file_filtering_hs_database() {
  hs_compile_error_t *compile_error = NULL;
  hs_error_t error_code =
      hs_compile(option_filter_file_pattern.c_str(), HS_FLAG_UTF8,
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

int main(int argc, char **argv) {

  argparse::ArgumentParser program("hg");

  program.add_argument("-c", "--count")
      .help("This flag suppresses normal output and shows the number of lines "
            "that match the given patterns for each file searched.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-n", "--line-number")
      .help("Show line numbers (1-based). This is enabled by default when "
            "searching in a terminal.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-N", "--no-line-number")
      .help("Suppress line numbers. This is enabled by default when not "
            "searching in a terminal.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-i", "--ignore-case")
      .help("When this flag is provided, the given patterns will be searched "
            "case insensitively.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-l", "--files-with-matches")
      .help("print only filenames")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-w", "--word-regexp")
      .help(
          "Only show matches surrounded by word boundaries. This is equivalent "
          "to putting \b before and after all of the search patterns.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-f", "--filter")
      .help("Filter files based on a pattern");

  const auto max_concurrency = std::thread::hardware_concurrency();
  const auto default_num_threads =
      max_concurrency > 1 ? max_concurrency - 1 : 1;
  program.add_argument("-j", "--threads")
      .help("The approximate number of threads to use")
      .default_value(default_num_threads)
      .scan<'d', unsigned>();

  program.add_argument("pattern").required().help("regular expression pattern");

  program.add_argument("path")
      .default_value(std::string{"."})
      .help("path to search");

  try {
    program.parse_args(argc, argv);
  } catch (const std::runtime_error &err) {
    std::cerr << err.what() << std::endl;
    std::cerr << program;
    return 1;
  }

  option_count_matching_lines = program.get<bool>("-c");
  auto N = program.get<unsigned>("-j");
  auto show_line_number = program.get<bool>("-n");
  auto hide_line_number = program.get<bool>("-N");
  option_ignore_case = program.get<bool>("-i");
  option_print_only_filenames = program.get<bool>("-l");
  if (program.is_used("-f")) {
    option_filter_file_pattern = program.get<std::string>("-f");
    option_filter_files = true;
    if (!construct_file_filtering_hs_database()) {
      throw std::runtime_error("Error compiling pattern " +
                               option_filter_file_pattern);
    }
  }
  auto pattern = program.get<std::string>("pattern");
  auto path = program.get<std::string>("path");

  // Check if word boundary is requested
  if (program.get<bool>("-w")) {
    pattern = "\\b" + pattern + "\\b";
  }

  is_stdout = isatty(STDOUT_FILENO) == 1;

  if (is_stdout) {
    // By default show line numbers
    // unless -N is used
    option_show_line_numbers = (!hide_line_number);
  } else {
    // By default hide line numbers
    // unless -n is used
    option_show_line_numbers = show_line_number;
  }

  hs_compile_error_t *compile_error = NULL;
  hs_error_t error_code =
      hs_compile(pattern.c_str(),
                 (option_ignore_case ? HS_FLAG_CASELESS : 0) | HS_FLAG_UTF8 |
                     HS_FLAG_SOM_LEFTMOST,
                 HS_MODE_BLOCK, NULL, &database, &compile_error);
  if (error_code != HS_SUCCESS) {
    fprintf(stderr, "Error compiling pattern: %s\n", compile_error->message);
    hs_free_compile_error(compile_error);
    return 1;
  }

  hs_error_t database_error = hs_alloc_scratch(database, &scratch);
  if (database_error != HS_SUCCESS) {
    fprintf(stderr, "Error allocating scratch space\n");
    hs_free_database(database);
    return 1;
  }

  if (std::filesystem::is_regular_file(path)) {
    // Set up the scratch space
    hs_scratch_t *local_scratch = NULL;
    hs_error_t database_error = hs_alloc_scratch(database, &local_scratch);
    if (database_error != HS_SUCCESS) {
      fprintf(stderr, "Error allocating scratch space\n");
      hs_free_database(database);
      return false;
    }
    thread_local_scratch.push_back(local_scratch);

    // Set up the scratch space per line
    hs_scratch_t *scratch_per_line = NULL;
    database_error = hs_alloc_scratch(database, &scratch_per_line);
    if (database_error != HS_SUCCESS) {
      fprintf(stderr, "Error allocating scratch space\n");
      hs_free_database(database);
      return false;
    }
    thread_local_scratch_per_line.push_back(scratch_per_line);
    process_file_mmap(std::move(path));
  } else {

    // Initialize libgit2
    git_libgit2_init();

    std::vector<std::thread> consumer_threads(N);

    thread_local_scratch.reserve(N);
    thread_local_scratch_per_line.reserve(N);

    for (std::size_t i = 0; i < N; ++i) {
      // Set up the scratch space
      hs_scratch_t *local_scratch = NULL;
      hs_error_t database_error = hs_alloc_scratch(database, &local_scratch);
      if (database_error != HS_SUCCESS) {
        fprintf(stderr, "Error allocating scratch space\n");
        hs_free_database(database);
        return false;
      }
      thread_local_scratch.push_back(local_scratch);

      // Set up the scratch space per line
      hs_scratch_t *scratch_per_line = NULL;
      database_error = hs_alloc_scratch(database, &scratch_per_line);
      if (database_error != HS_SUCCESS) {
        fprintf(stderr, "Error allocating scratch space\n");
        hs_free_database(database);
        return false;
      }
      thread_local_scratch_per_line.push_back(scratch_per_line);

      consumer_threads[i] = std::thread([i = i]() {
        char buffer[FILE_CHUNK_SIZE];
        std::string lines{};
        while (true) {
          if (num_files_enqueued > 0) {
            visit_one(i, buffer, lines);
            if (!running && num_files_dequeued == num_files_enqueued) {
              break;
            }
          }
        }
      });
    }

    // Try to visit as a git repo
    // If it fails, visit normally
    if (!visit_git_repo(path)) {
      visit(path);
    }

    running = false;

    for (std::size_t i = 0; i < N; ++i) {
      consumer_threads[i].join();
    }
  }

  hs_free_scratch(scratch);
  hs_free_database(database);

  return 0;
}

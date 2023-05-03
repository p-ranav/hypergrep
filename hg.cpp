#include "concurrentqueue.h"
#include <atomic>
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <fmt/color.h>
#include <fmt/format.h>
#include <fstream>
#include <hs/hs.h>
#include <numeric>
#include <set>
#include <thread>
#include <vector>
#include <unistd.h>
#include <git2.h>

git_repository* repo = nullptr;
git_index* repo_index = nullptr; 

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

struct file_context {
  std::atomic<size_t> &number_of_matches;
  std::set<std::pair<unsigned long long, unsigned long long>> &matches;
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

void process_matches(const char *filename, char *buffer, std::size_t bytes_read,
                     file_context &ctx, std::size_t &current_line_number,
                     std::string &lines) {
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
        lines += fmt::format(fg(fmt::color::green), "{}", current_line_number);
        lines += fmt::format(":{}\n", line);
      } else {
        lines += fmt::format("{}\n", line);
      }
    } else {
      if (option_show_line_numbers) {
        lines += fmt::format("{}:{}:{}\n", filename, current_line_number, line);
      } else {
        lines += fmt::format("{}:{}\n", filename, line);
      }
    }
  }
}

bool process_file(std::string &&filename, std::size_t i, char *buffer, std::string& lines) {
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
    file_context ctx{number_of_matches, matches};

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
      }

      if (last_newline && bytes_read > search_size && bytes_read == FILE_CHUNK_SIZE) /* Not the last chunk */ {

        if ((last_newline - buffer) == bytes_read - 1) {
          // Chunk ends exactly at the newline
          // Do nothing

          // TODO
          // There are probably other conditions too where lseek can be avoided
          // Research this
        }
        else
        {
          // Backtrack "remainder" number of characters
          // auto start = std::chrono::high_resolution_clock::now();
          lseek(fd, -1 * (bytes_read - search_size), SEEK_CUR);
          // auto end = std::chrono::high_resolution_clock::now();
          // fmt::print("{}us\n", (std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()));
        }
      }
  }

  close(fd);

  if (result && option_print_only_filenames) {
    if (is_stdout) {
      fmt::print(fg(fmt::color::steel_blue), "{}\n", filename);
    } else {
      fmt::print("{}\n", filename);
    }
  } else if (result && !lines.empty()) {
    if (is_stdout) {
      lines = fmt::format(fg(fmt::color::steel_blue), "\n{}\n", filename) + lines;
      fmt::print("{}", lines);
    } else {
      fmt::print("{}", lines);
    }
  }

  lines.clear();
  return result;
}

bool visit_git_repo(const std::filesystem::path& dir, git_repository* repo = nullptr);

void search_submodules(const char* dir, git_repository* this_repo) {
  git_submodule_foreach(this_repo, [](git_submodule* sm, const char* name, void* payload) -> int {

    const char* dir = (const char*)(payload);
    auto path = std::filesystem::path(dir);
    
    git_repository* sm_repo = nullptr;
    if (git_submodule_open(&sm_repo, sm) == 0) {
      auto submodule_path = path / name;      
      visit_git_repo(submodule_path, sm_repo);
    }	  
    return 0;
  }, (void*)dir);
}

void visit_git_index(const std::filesystem::path& dir, git_index* index) {
  std::size_t n{0};
  while (true) {
    auto entry = git_index_get_byindex(index, n++);
    if (!entry) {
      break;
    }
    const auto path = dir / entry->path;
    queue.enqueue(ptok, path.string());
    ++num_files_enqueued;    
  }
}

bool visit_git_repo(const std::filesystem::path& dir, git_repository* repo) {
  
  if (!repo) {
    // Open the repository
    if (git_repository_open(&repo, dir.c_str()) != 0) {
      return false;
    }
  }

  // Load the git index for this repository
  git_index* index = nullptr;  
  if (git_repository_index(&index, repo) != 0) {
    return false;    
  }

  // Visit each entry in the index
  visit_git_index(dir, index);

  // Search the submodules inside this submodule
  search_submodules(dir.c_str(), repo);

  git_index_free(index);
  git_repository_free(repo);
  return true;
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

      queue.enqueue(ptok, path.native());
      ++num_files_enqueued;
    } else if (it->is_directory()) {
      if (filename_cstr[0] == '.' || it->is_symlink()) {        
        // Stop processing this directory and its contents
        it.disable_recursion_pending();
      }
    }
  }
}

static inline bool visit_one(const std::size_t i, char *buffer, std::string& lines) {
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

int main(int argc, char **argv) {
  int opt;
  while ((opt = getopt(argc, argv, "nilI")) != -1) {
    switch (opt) {
    case 'n':
      option_show_line_numbers = true;
      break;
    case 'i':
      option_ignore_case = true;
      break;
    case 'l':
      option_print_only_filenames = true;
      break;
    default:
      fprintf(stderr, "Usage: %s [-n] [-i] [-l] [-I] pattern [filename]\n",
              argv[0]);
      exit(EXIT_FAILURE);
    }
  }

  const char *pattern = argv[optind];
  const char *path = ".";
  if (optind + 1 < argc) {
    path = argv[optind + 1];
  }

  is_stdout = isatty(STDOUT_FILENO) == 1;

  hs_compile_error_t *compile_error = NULL;
  hs_error_t error_code =
      hs_compile(pattern,
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

    std::string path_string(path);
    char buffer[FILE_CHUNK_SIZE];
    std::string lines{};
    process_file(std::move(path_string), 0, buffer, lines);
  } else {

    // Initialize libgit2
    git_libgit2_init();
    
    const auto N = std::thread::hardware_concurrency();
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

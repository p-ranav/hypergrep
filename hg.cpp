#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <hs/hs.h>
#include "concurrentqueue.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

std::size_t get_file_size(const char* filename) {
  struct stat st;
  if(stat(filename, &st) != 0) {
    return 0;
  }
  return st.st_size;
}

moodycamel::ConcurrentQueue<std::string> queue;
moodycamel::ProducerToken ptok(queue);

bool is_stdout{true};
std::atomic<bool> running{true};
std::atomic<std::size_t> num_files_enqueued{0};
std::atomic<std::size_t> num_files_dequeued{0};
hs_database_t *database = NULL;
hs_scratch_t *scratch = NULL;
std::mutex cout_mutex;

struct file_context
{
  const char *filename;
  const char *data;
  std::size_t size;
  std::string &lines;
  std::size_t &current_line_number;
  const char **current_ptr;
  hs_scratch *local_scratch;
};

std::size_t count_newlines(const char *start, const char *end)
{
  if (end > start)
  {
    return std::count(start, end, '\n');
  }
  else
  {
    return 0;
  }
}

struct line_context
{
  const char *data;
  std::string &lines;
  const char **current_ptr;
};

static int print_match_in_red_color(unsigned int id, unsigned long long from,
                                    unsigned long long to, unsigned int flags, void *ctx)
{
  auto *fctx = (line_context *)(ctx);
  const char *line_data = fctx->data;
  auto &lines = fctx->lines;
  const char *start = *(fctx->current_ptr);
  lines += std::string(start, line_data + from - start);
  lines += "\033[31m";
  lines += std::string(&line_data[from], to - from);
  lines += "\033[0m";
  *(fctx->current_ptr) = line_data + to;
  return 0;
}

static int on_match(unsigned int id, unsigned long long from,
                    unsigned long long to, unsigned int flags, void *ctx)
{
  // print line with match
  auto *fctx = (file_context *)(ctx);

  if (fctx)
  {
    auto &lines = fctx->lines;
    auto size = fctx->size;
    std::size_t &current_line_number = fctx->current_line_number;
    const char **current_ptr = fctx->current_ptr;

    if (fctx->data)
    {
      auto data = std::string_view(fctx->data, size);

      auto end = data.find_first_of('\n', to);
      if (end == std::string_view::npos)
      {
        end = size;
      }

      auto start = data.find_last_of('\n', from);
      if (start == std::string_view::npos)
      {
        start = 0;
      }
      else
      {
        start += 1;
      }

      const std::size_t previous_line_number = fctx->current_line_number;
      const auto line_count = count_newlines(*current_ptr, fctx->data + start);
      current_line_number += line_count;
      *current_ptr = fctx->data + end;

      if (current_line_number == previous_line_number && previous_line_number > 0)
      {
        return 0;
      }

      if (from >= start && to >= from && end >= to && end >= start)
      {
        if (is_stdout)
        {
          lines += "\033[32m";
          lines += std::to_string(current_line_number);
          lines += "\033[0m";
          lines += ":";

          std::string_view line(&data[start], end - start);	
          const char *line_ptr = line.data();

          line_context nested_ctx{line_ptr, lines, &line_ptr};
          if (hs_scan(database, &data[start], end - start, 0, fctx->local_scratch, print_match_in_red_color, &nested_ctx) != HS_SUCCESS)
          {
            return 1;
          }

          if (line_ptr != (&data[start] + end - start))
          {
            // some left over
            lines += std::string(line_ptr, &data[start] + end - start - line_ptr);
          }
          lines += '\n';
        }
        else
        {
          lines += std::string(fctx->filename) + ":" + std::to_string(current_line_number) + ":" + std::string(&data[start], end - start) + "\n";
        }
      }
    }
  }

  return 0;
}

bool process_file(std::string_view filename, std::size_t file_size)
{
  constexpr std::size_t MMAP_LOWER_THRESHOLD = 2 * 1024 * 1024;

  char *file_data;
  int   fd = open(filename.data(), O_RDONLY, 0);
  if (fd == -1)
  {
    return false;
  }
  if (file_size > MMAP_LOWER_THRESHOLD) // check if file size is larger than 1 MB
  {
      file_data = (char *)mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
      close(fd);
      if (file_data == MAP_FAILED)
      {
          std::lock_guard<std::mutex> lock{cout_mutex};
          std::cout << "Error: Failed to mmap file: " << filename << std::endl;
          return false;
      }
  }
  else
  {
      char* buffer = new char[file_size];
      auto ret = read(fd, buffer, file_size);
      if (ret != file_size)
      {
          // failed to read the entire file
          // handle error here
      }
      close(fd);
      file_data = buffer;
  }

  // Set up the scratch space
  hs_scratch_t *local_scratch = NULL;
  hs_error_t database_error = hs_clone_scratch(scratch, &local_scratch);
  if (database_error != HS_SUCCESS)
  {
    fprintf(stderr, "Error allocating scratch space\n");
    hs_free_database(database);
    return false;
  }

  // Set up the scratch space for handling multiple occurrences within a line
  // hs_scan is used on the line to color code multiple matches within a line
  hs_scratch_t *local_scratch_for_line = NULL;
  database_error = hs_clone_scratch(local_scratch, &local_scratch_for_line);
  if (database_error != HS_SUCCESS)
  {
    fprintf(stderr, "Error allocating scratch space\n");
    hs_free_database(database);
    return false;
  }

  bool result{true};

  // Process the entire buffer
  std::size_t current_line_number{1};
  const char *current_ptr{file_data};
  std::string lines{""};
  file_context ctx{filename.data(), file_data, static_cast<std::size_t>(file_size), lines, current_line_number, &current_ptr, local_scratch_for_line};
  if (hs_scan(database, file_data, file_size, 0, local_scratch, on_match, (void *)(&ctx)) == HS_SUCCESS)
  {
    if (!lines.empty())
    {
      if (lines.find('\0') != std::string::npos)
      {
        // line has NULL bytes
        // File was probably a binary file
        result = false;
      }
      else
      {
        std::lock_guard<std::mutex> lock{cout_mutex};
        if (is_stdout)
        {
          std::cout << "\n"
                    << filename << "\n";
          std::cout << lines;
        }
        else
        {
          std::cout << lines;
        }
      }
    }
    result = true;
  }
  else
  {
    // file was ignored inside hs_scan
    // by checking is_ignored() at the first match
    result = false;
  }

  if (file_size > MMAP_LOWER_THRESHOLD)
  {
      munmap((void *)file_data, file_size);
  }
  else
  {
      delete[] file_data;
  }  
  
  hs_free_scratch(local_scratch_for_line);
  hs_free_scratch(local_scratch);

  return result;
}

void visit(const char *path)
{
  for (const auto &entry : std::filesystem::recursive_directory_iterator(path, std::filesystem::directory_options::skip_permission_denied))
  {
    if (std::filesystem::is_regular_file(path) && !std::filesystem::is_symlink(path))
    {
      queue.enqueue(ptok, entry.path().c_str());
      num_files_enqueued += 1;
    }
  }
}

static inline bool visit_one()
{
  std::string entry;
  auto found = queue.try_dequeue_from_producer(ptok, entry);
  if (found)
  {
    num_files_dequeued += 1;
    process_file(entry.data(), get_file_size(entry.data()));
    return true;
  }
  else
  {
    return false;
  }
}

int main(int argc, char **argv)
{
  is_stdout = isatty(STDOUT_FILENO) == 1;
  std::ios_base::sync_with_stdio(false);
  std::cin.tie(NULL);

  if (argc < 2)
  {
    std::cerr << "Usage: " << argv[0] << " <filename>" << std::endl;
    return 1;
  }

  // const char *pattern = argv[1];
  auto* pattern = argv[1];
  const char* path = ".";
  if (argc > 2)
  {
    path = argv[2];
  }

  hs_compile_error_t *compile_error = NULL;
  hs_error_t error_code = hs_compile(pattern, HS_FLAG_SOM_LEFTMOST, HS_MODE_BLOCK, NULL, &database, &compile_error);
  if (error_code != HS_SUCCESS)
  {
    fprintf(stderr, "Error compiling pattern: %s\n", compile_error->message);
    hs_free_compile_error(compile_error);
    return 1;
  }

  hs_error_t database_error = hs_alloc_scratch(database, &scratch);
  if (database_error != HS_SUCCESS)
  {
    fprintf(stderr, "Error allocating scratch space\n");
    hs_free_database(database);
    return 1;
  }

  std::vector<std::thread> consumer_threads{};
  const auto N = std::thread::hardware_concurrency();

  for (std::size_t i = 0; i < N; ++i)
  {
    consumer_threads.push_back(std::thread([]()
                                           {
      while (true) {
        visit_one();

        if (!running && num_files_dequeued == num_files_enqueued) {
          break;
        }
      } }));
  }

  visit(path);
  running = false;

  for (std::size_t i = 0; i < N; ++i)
  {
    consumer_threads[i].join();
  }

  hs_free_scratch(scratch);
  hs_free_database(database);

  return 0;
}

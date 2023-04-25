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
#include <fmt/format.h>

#include <stdbool.h>
#include <stdint.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <stdbool.h>
#include <stdint.h>

bool is_elf_header(const char* buffer) {
    const char* elf_magic = "\x7f""ELF";
    size_t magic_len = strlen(elf_magic);

    // Compare first few bytes to archive magic string
    return (strncmp(buffer, elf_magic, magic_len) == 0);
}

bool is_archive_header(const char* buffer) {
    const char* archive_magic = "!<arch>";
    size_t magic_len = strlen(archive_magic);

    // Compare first few bytes to archive magic string
    return (strncmp(buffer, archive_magic, magic_len) == 0);
}

std::size_t get_file_size(std::string& filename) {
  struct stat st;
  if(lstat(filename.data(), &st) != 0) {
    return 0;
  }

  // Symbolic link or directory is ignored
  if (S_ISLNK(st.st_mode) || S_ISDIR(st.st_mode))
  {
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

std::vector<hs_scratch*> thread_local_scratch;
std::vector<hs_scratch*> thread_local_scratch_per_line;

struct file_context
{
  std::string &filename;
  const char *data;
  std::size_t &size;
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
  auto* fctx = static_cast<line_context*>(ctx);
  const char *line_data = fctx->data;
  auto &lines = fctx->lines;
  const char *start = *(fctx->current_ptr);
  const size_t len = to - from;

  lines.reserve(lines.size() + len + 9);
  lines.append(start, line_data + from);
  lines.append("\033[31m", 5);
  lines.append(&line_data[from], len);
  lines.append("\033[0m", 4);
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
    auto &size = fctx->size;
    std::size_t &current_line_number = fctx->current_line_number;

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
      const auto line_count = count_newlines(fctx->data, fctx->data + start);
      current_line_number += line_count;

      if (current_line_number == previous_line_number && previous_line_number > 0)
      {
        return 0;
      }

      if (from >= start && to >= from && end >= to && end >= start)
      {
        if (is_stdout)
        {
          std::string_view line(&data[start], end - start);	
          lines += "\033[32m" + std::to_string(current_line_number) + "\033[0m" + ":";

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
          lines += fmt::format("{}:{}:{}\n", fctx->filename, current_line_number, std::string_view(&data[start], end - start));
        }
      }
    }
  }

  return 0;
}

// bool process_file(std::string&& filename, std::size_t file_size, std::size_t i)
// {
//   char *file_data;
//   int   fd = open(filename.data(), O_RDONLY, 0);
//   if (fd == -1)
//   {
//     return false;
//   }
//   char* buffer = new char[file_size];
//   auto ret = read(fd, buffer, file_size);
//   close(fd);
//   if (ret != file_size)
//   {
//     return false;
//   }
//   else
//   {
//     file_data = buffer;
//   }

//   // Set up the scratch space
//   hs_scratch_t *local_scratch = thread_local_scratch[i];
//   hs_scratch_t *local_scratch_per_line = thread_local_scratch_per_line[i];

//   bool result{true};

//   // Process the entire buffer
//   std::size_t current_line_number{1};
//   const char *current_ptr{file_data};
//   std::string lines{""};
//   file_context ctx{filename, file_data, file_size, lines, current_line_number, &current_ptr, local_scratch_per_line};
//   if (hs_scan(database, file_data, file_size, 0, local_scratch, on_match, (void *)(&ctx)) == HS_SUCCESS)
//   {
//     if (!lines.empty())
//     {
//       // std::lock_guard<std::mutex> lock{cout_mutex};
//       if (is_stdout)
//       {
//         fmt::print("\n{}\n{}", filename, lines);
//       }
//       else
//       {
//         fmt::print("{}", lines);
//       }
//     }
//     result = true;
//   }
//   else
//   {
//     // file was ignored inside hs_scan
//     // by checking is_ignored() at the first match
//     result = false;
//   }

//   delete[] file_data;  
  
//   return result;
// }

bool process_file(std::string&& filename, std::size_t file_size, std::size_t i)
{
  char *file_data;
  int   fd = open(filename.data(), O_RDONLY, 0);
  if (fd == -1)
  {
    return false;
  }
  bool result{true};

  // Set up the scratch space
  hs_scratch_t *local_scratch = thread_local_scratch[i];
  hs_scratch_t *local_scratch_per_line = thread_local_scratch_per_line[i];

  // Process the file in chunks
  const std::size_t CHUNK_SIZE = 10 * 4096; // 1MB chunk size
  char buffer[CHUNK_SIZE];
  std::size_t bytes_read = 0;
  std::size_t current_line_number{1};
  std::string lines{""};

  std::size_t iterations{0};

  while (bytes_read < file_size) {
    // Read the next chunk
    auto bytes_to_read = std::min(file_size - bytes_read, CHUNK_SIZE);
    auto ret = read(fd, buffer, bytes_to_read);
    if (ret != bytes_to_read) {
      result = false;
      break;
    }

    if (iterations == 0)
    {
      if (bytes_to_read >= 4 && (is_elf_header(buffer) || is_archive_header(buffer)))
      {
        result = false;
        break;
      }
    }

    // Process the current chunk
    file_context ctx{filename, buffer, bytes_to_read, lines, current_line_number, nullptr, local_scratch_per_line};
    if (hs_scan(database, buffer, bytes_to_read, 0, local_scratch, on_match, (void *)(&ctx)) != HS_SUCCESS) {
      result = false;
      break;
    }

    bytes_read += bytes_to_read;
    iterations += 1;
  }

  close(fd);

  if (result && !lines.empty())
  {
    // std::lock_guard<std::mutex> lock{cout_mutex};
    if (is_stdout)
    {
      fmt::print("\n{}\n{}", filename, lines);
    }
    else
    {
      fmt::print("{}", lines);
    }
  }

  return result;
}

void visit(const char *path)
{
  for (auto&& entry : std::filesystem::directory_iterator(path, std::filesystem::directory_options::skip_permission_denied))
  {
    const auto& path = entry.path();
    const auto& filename = path.filename();
    const char* pathstring = path.c_str();
    if (filename.c_str()[0] == '.') continue;

    if (entry.is_directory())
    {
      visit(pathstring);
    }
    else if (entry.is_regular_file())
    {
      ++num_files_enqueued;
      queue.enqueue(ptok, path.c_str());
    }
  }
}

static inline bool visit_one(const std::size_t i)
{
  std::string entry;
  auto found = queue.try_dequeue_from_producer(ptok, entry);
  if (found)
  {
    const auto file_size = get_file_size(entry);
    if (file_size > 0)
    {
      process_file(std::move(entry), file_size, i);
    }
    num_files_dequeued += 1;
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

  auto start = std::chrono::high_resolution_clock::now();
  std::vector<std::thread> consumer_threads{};
  const auto N = std::thread::hardware_concurrency();

  thread_local_scratch.reserve(N);
  thread_local_scratch_per_line.reserve(N);

  for (std::size_t i = 0; i < N; ++i)
  {

    // Set up the scratch space
    hs_scratch_t *local_scratch = NULL;
    hs_error_t database_error = hs_alloc_scratch(database, &local_scratch);
    if (database_error != HS_SUCCESS)
    {
      fprintf(stderr, "Error allocating scratch space\n");
      hs_free_database(database);
      return false;
    }
    thread_local_scratch.push_back(local_scratch);

    // Set up the scratch space per line
    hs_scratch_t *scratch_per_line = NULL;
    database_error = hs_alloc_scratch(database, &scratch_per_line);
    if (database_error != HS_SUCCESS)
    {
      fprintf(stderr, "Error allocating scratch space\n");
      hs_free_database(database);
      return false;
    }
    thread_local_scratch_per_line.push_back(scratch_per_line);

    consumer_threads.push_back(std::thread([i = i]()
                                           {
      while (true) {
        if (!visit_one(i))
        {
          if (!running && num_files_dequeued == num_files_enqueued) {
            break;
          }
        }
      } }));
  }

  visit(path);
  running = false;

  for (std::size_t i = 0; i < N; ++i)
  {
    consumer_threads[i].join();
  }
  auto end = std::chrono::high_resolution_clock::now();
  fmt::print("{} secs\n", (std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() / 1000.0f));

  hs_free_scratch(scratch);
  hs_free_database(database);

  return 0;
}

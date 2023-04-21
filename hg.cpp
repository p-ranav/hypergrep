#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <git2.h>
#include <hs/hs.h>
#include "concurrentqueue.h"

moodycamel::ConcurrentQueue<std::string> queue;
moodycamel::ProducerToken ptok(queue);

std::atomic<bool> running{true};
std::atomic<std::size_t> matches{0};
std::atomic<std::size_t> num_files_enqueued{0};
std::atomic<std::size_t> num_files_dequeued{0};
std::atomic<std::size_t> num_files_searched{0};
std::atomic<std::size_t> num_files_contained_matches{0};
git_repository *repo = nullptr;
hs_database_t *database = NULL;
hs_scratch_t *scratch = NULL;
std::mutex cout_mutex;

// 200-400us
bool is_ignored(const char *path)
{
  if (!repo)
  {
    return false;
  }
  int ignored = 0; // 0 -> not ignored, 1 -> ignored
  if (git_ignore_path_is_ignored(&ignored, repo, path) < 0)
  {
    // failed to check if path is ignored
    return false;
  }

  return (bool)(ignored);
}

struct file_context
{
  const char *filename;
  const char *data;
  std::size_t size;
  std::string &lines;
  std::size_t &current_line_number;
  const char **current_ptr;
  hs_scratch* local_scratch;
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
  const char** current_ptr;
};

static int print_match_in_red_color(unsigned int id, unsigned long long from,
  unsigned long long to, unsigned int flags, void *ctx)
{
  auto *fctx = (line_context *)(ctx);
  const char* line_data = fctx->data;
  auto &lines = fctx->lines;
  const char* start = *(fctx->current_ptr);
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

  if (is_ignored(fctx->filename))
  {
    return 1;
  }

  matches += 1;

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

      if (current_line_number == previous_line_number && previous_line_number > 0) {
        return 0;
      }

      if (from > start && to > from && end > to && end > start)
      {
        lines += "\033[32m";
        lines += std::to_string(current_line_number);
        lines += "\033[0m";
        lines += ":";

        std::string_view line(&data[start], end - start);
        const char* line_ptr = line.data();
        line_context nested_ctx{line_ptr, lines, &line_ptr};
        if (hs_scan(database, &data[start], end - start, 0, fctx->local_scratch, print_match_in_red_color, &nested_ctx) != HS_SUCCESS)
        {
          return 1;
        }

        if (line_ptr != (&data[start] + end - start)) {
          // some left over 
          lines += std::string(line_ptr, &data[start] + end - start - line_ptr);
        }
        lines += '\n';

      }
    }
  }

  return 0;
}

bool process_file(std::string_view filename)
{
  std::ifstream file(filename.data(), std::ios::binary);
  if (!file)
  {
    std::cerr << "Failed to open file: " << filename << std::endl;
    return false;
  }

  auto fsize = file.tellg();
  file.seekg(0, std::ios::end);
  fsize = file.tellg() - fsize;
  file.seekg(0, std::ios::beg);
  const auto file_size = fsize;

  char *file_data = new char[file_size];

  // Read the entire file into a big buffer
  file.read(file_data, file_size);

  const auto NL = std::min(static_cast<std::size_t>(file_size), std::size_t(256));
  auto binary_file = std::find(file_data, file_data + NL, '\0') != file_data + NL;
  if (binary_file)
  {
    // early exit
    return false;
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
      std::lock_guard<std::mutex> lock{cout_mutex};
      std::cout << "\n"
                << std::filesystem::relative(filename).c_str() << "\n";
      std::cout << lines;
      num_files_contained_matches += 1;
    }
    result = true;
  }
  else
  {
    // file was ignored inside hs_scan
    // by called is_ignored at the first match
    result = false;
  }

  delete[] file_data;
  hs_free_scratch(local_scratch_for_line);
  hs_free_scratch(local_scratch);

  return result;
}

static inline int visit(const char *path)
{
  std::filesystem::directory_iterator iter(path, std::filesystem::directory_options::skip_permission_denied);
  for (const auto &entry : iter)
  {

    if (entry.is_symlink())
    {
      continue;
    }

    const char *filename = entry.path().filename().c_str();
    std::string_view filepath = entry.path().c_str();

    // Check if path is a directory
    if (entry.is_directory())
    {
      if (filename[0] != '.')
      {
        if (!is_ignored(filepath.data()))
        {
          visit(filepath.data());
        }
      }
    }
    // Check if path is a regular file
    else if (entry.is_regular_file())
    {
      if (filename[0] != '.')
      {
        queue.enqueue(ptok, std::string(filepath));
        num_files_enqueued += 1;
      }
    }
  }

  return 0;
}

bool visit_one()
{
  std::string filepath{};
  auto found = queue.try_dequeue_from_producer(ptok, filepath);
  if (found)
  {
    num_files_dequeued += 1;
    if (process_file(filepath))
    {
      num_files_searched += 1;
    }
    return true;
  }
  else
  {
    return false;
  }
}

int main(int argc, char **argv)
{
  std::ios_base::sync_with_stdio(false);

  if (argc < 2)
  {
    std::cerr << "Usage: " << argv[0] << " <filename>" << std::endl;
    return 1;
  }

  const char *pattern = argv[1];
  const char *path = argv[2];

  char resolved_path[PATH_MAX];
  char *ret = realpath(path, resolved_path);
  if (ret == NULL)
  {
    std::cerr << "Error with realpath\n";
    return 1;
  }

  git_libgit2_init();
  if (git_repository_open(&repo, resolved_path) < 0)
  {
    // failed to open repository
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

  auto start = std::chrono::high_resolution_clock::now();
  if (visit(resolved_path) == -1)
  {
    return 1;
  }
  running = false;

  for (std::size_t i = 0; i < N; ++i)
  {
    consumer_threads[i].join();
  }
  auto end = std::chrono::high_resolution_clock::now();

  std::cout << "\n"
            << matches << " matches\n";
  std::cout << num_files_contained_matches << " files contained matches\n";
  std::cout << num_files_searched << " files searched\n";
  std::cout << (std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() / 1000.0f) << " seconds spent searching\n";

  git_repository_free(repo);
  hs_free_scratch(scratch);
  hs_free_database(database);

  return 0;
}

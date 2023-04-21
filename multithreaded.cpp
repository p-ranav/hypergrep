#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <git2.h>
#include <hs/hs.h>
#include <algorithm>
#include <atomic>
#include <sstream>
#include <sys/stat.h>
#include <filesystem>
#include "concurrentqueue.h"
moodycamel::ConcurrentQueue<std::string> queue;
std::atomic<bool> running{true};
std::atomic<std::size_t> n{0};
std::atomic<std::size_t> n_consumed{0};

#define START   auto start = std::chrono::high_resolution_clock::now();
#define STOP   auto end = std::chrono::high_resolution_clock::now(); \
  auto diff = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count(); \
  if (diff > 0) { \
    std::cout << diff << "\n"; \
  }

git_repository* repo = nullptr;
hs_database_t *database = NULL;
hs_scratch_t *scratch = NULL;

// 200-400us
bool is_ignored(const char* path)
{
    if (!repo) {
        return false;
    }
    int ignored = 0; // 0 -> not ignored, 1 -> ignored
    if (git_ignore_path_is_ignored(&ignored, repo, path) < 0) {
        // failed to check if path is ignored
        return false;
    }

    return (bool)(ignored);
}

const std::streampos CHUNK_SIZE = 1024 * 1024;

std::mutex cout_mutex;

struct file_context {
    std::string& lines;
    const char* data;
    size_t size;
};

std::atomic<std::size_t> matches{0};

static int on_match(unsigned int id, unsigned long long from,
                       unsigned long long to, unsigned int flags, void *ctx)
{
  matches += 1;

  // print line with match
  auto* fctx = (file_context*)(ctx);
  if (fctx)
  {
    auto& lines = fctx->lines;
    auto size = fctx->size;

    if (fctx->data)
    {
      auto data = std::string_view(fctx->data, size);

      auto end = data.find_first_of('\n', to);
      if (end == std::string_view::npos) {
          end = size;
      }

      auto start = data.find_last_of('\n', from);
      if (start == std::string_view::npos) {
          start = 0;
      } else {
          start += 1;
      }

      if (from > start && to > from && end > to && end > start) {
        lines += std::string(&data[start], from - start);
        lines += "\033[31m";
        lines += std::string(&data[from], to - from);
        lines += "\033[0m";
        lines += std::string(&data[to], end - to);
        lines += '\n';
      } 
    }
  }

  return 0;
}

// void process_file(std::string_view filename, const std::streampos file_size) {
//   std::ifstream file(filename.data(), std::ios::binary);
//   if (!file) {
//     std::cerr << "Failed to open file: " << filename << std::endl;
//     return;
//   }

//   const std::size_t first_chunk_size = 256; // read first 256 bytes to check if binary
//   const std::size_t chunk_size = 1024 * 1024; // read in 1 MB chunks
//   char* chunk_data = new char[chunk_size];

//   // Read first chunk to check if binary
//   file.read(chunk_data, first_chunk_size);
//   bool is_binary = std::find(chunk_data, chunk_data + first_chunk_size, '\0') != chunk_data + first_chunk_size;

//   if (is_binary) {
//     delete[] chunk_data;
//     return;
//   }

//   std::string lines;

//   std::streampos bytes_read = first_chunk_size;
//   while (bytes_read < file_size) {
//     const std::streampos remaining_bytes = file_size - bytes_read;
//     const std::size_t bytes_to_read = static_cast<std::size_t>(std::min<std::streampos>(remaining_bytes, chunk_size));

//     file.read(chunk_data, bytes_to_read);

//     // Process the chunk here
//     file_context ctx { lines, chunk_data, bytes_to_read };
//     hs_scan(database, chunk_data, bytes_to_read, 0, scratch, on_match, (void *)(&ctx));

//     bytes_read += bytes_to_read;
//   }

//   if (!lines.empty())
//   {
//     std::lock_guard<std::mutex> lock{cout_mutex};
//     std::cout << "\n" << filename << "\n";
//     std::cout << lines;
//   }

//   delete[] chunk_data;
// }

std::streampos get_file_size_and_reset(std::ifstream& file)
{
    const std::streampos fileSize = file.tellg(); // get file size
    file.seekg(0, std::ios::beg); // reset file pointer to the beginning
    return fileSize;
}

bool process_file(std::string_view filename) {
  std::ifstream file(filename.data(), std::ios::binary);
  if (!file) {
    std::cerr << "Failed to open file: " << filename << std::endl;
    return false;
  }

  auto fsize = file.tellg();
  file.seekg( 0, std::ios::end );
  fsize = file.tellg() - fsize;
  file.seekg(0, std::ios::beg);
  const auto file_size = fsize;

  const std::size_t chunk_size = 1024; // read in 1 MB chunks
  char* file_data = new char[file_size];

  // Read the entire file into a big buffer
  file.read(file_data, file_size);

  std::string lines{""};

  // Set up the scratch space
  hs_scratch_t *local_scratch = NULL;
  hs_error_t database_error = hs_clone_scratch(scratch, &local_scratch);
  if (database_error != HS_SUCCESS)
  {
      fprintf(stderr, "Error allocating scratch space\n");
      hs_free_database(database);
      return false;
  }

  // Process the entire buffer
  file_context ctx { lines, file_data, file_size };
  hs_scan(database, file_data, file_size, 0, local_scratch, on_match, (void *)(&ctx));

  if (!lines.empty())
  {
    std::lock_guard<std::mutex> lock{cout_mutex};
    std::cout << "\n" << filename << "\n";
    std::cout << lines;
  }

  delete[] file_data;
  hs_free_scratch(local_scratch);
  return true;
}

static inline int visit(const char *path) {
    DIR *dir = opendir(path);
    if (dir == NULL) {
        // perror("opendir");
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {

        // Construct path // 8-10us
        const size_t path_len = strlen(path);
        const size_t name_len = _D_EXACT_NAMLEN(entry);
        const size_t total_len = path_len + 1 + name_len + 1;
        char filepath[total_len];
        memcpy(filepath, path, path_len);
        filepath[path_len] = '/';
        memcpy(filepath + path_len + 1, entry->d_name, name_len);
        filepath[total_len - 1] = '\0';

        // Process the file in chunks, multithreaded
        std::string_view filename = filepath;

        // Check if path is a directory
        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            // std::cout << filename << " is a DIR\n";
            if (!is_ignored(filepath))
            {
              visit(filename.data());
            }
        }
        // Check if path is a regular file 
        else if (entry->d_type == DT_REG) {
          queue.enqueue(std::string(filepath));
          n += 1;
        }
    }

    closedir(dir);
    return 0;
}

bool visit_one()
{
  std::string filepath;
  auto found = queue.try_dequeue(filepath);
  if (found) {
    process_file(filepath);
    n_consumed += 1;
    return true;
  } else {
    return false;
  }
}

int main(int argc, char** argv) {
  // std::ios_base::sync_with_stdio(false);

  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <filename>" << std::endl;
    return 1;
  }

  const char *pattern = argv[1];
  const char *path = argv[2];

  char resolved_path[PATH_MAX]; 
  char* ret = realpath(path, resolved_path); 
  if (ret == NULL) {
    std::cerr << "Error with realpath\n";
    return 1;
  }

  git_libgit2_init();
  if (git_repository_open(&repo, resolved_path) < 0) {
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
  static constexpr std::size_t N = 8;

  for (std::size_t i = 0; i < N; ++i)
  {
    consumer_threads.push_back(std::thread([]() {
      while (true) {
        visit_one();

        if (!running && n_consumed == n) {
          break;
        }
      }
    }));
  }

  if (visit(resolved_path) == -1) {
    return 1;
  }
  running = false;

  for (std::size_t i = 0; i < N; ++i)
  {
    consumer_threads[i].join();
  }

  std::cout << "\n" << matches << " in " << n_consumed << " files\n";

  git_repository_free(repo);
  hs_free_scratch(scratch);
  hs_free_database(database);

  return 0;
}

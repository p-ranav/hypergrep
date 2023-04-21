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
    const char* filename;
    char* data;
    size_t size;
};

size_t file_num{0};
size_t current_file_num{0};

static int on_match(unsigned int id, unsigned long long from,
                       unsigned long long to, unsigned int flags, void *ctx)
{
    // print line with match
    auto* fctx = (file_context*)(ctx);
    auto filename = fctx->filename;
    auto data = fctx->data;
    auto size = fctx->size;
    auto start = from, end = to;
    while (start > 0 && data[start] != '\n') {
        start--;
    }
    while (end < size && data[end] != '\n') {
        end++;
    }

    if (data[start] == '\n' && start + 1 < end) {
        start += 1;
    }

    if (file_num > current_file_num)
    {
        // Print filename
        const auto relative_path = std::filesystem::relative(filename, std::filesystem::current_path());
        printf("\n%s\n", relative_path.c_str());
        current_file_num = file_num;
    }

    if (end > start) {
      printf("%.*s", int(from - start), &data[start]);
      printf("\033[31m%.*s\033[0m", int(to - from), &data[from]);
      printf("\033[0m%.*s\n", int(end - to), &data[to]);
    }

    return 0;
}

void process_file(std::string_view filename, const std::streampos file_size) {
  std::ifstream file(filename.data(), std::ios::binary);
  if (!file) {
    std::cerr << "Failed to open file: " << filename << std::endl;
    return;
  }

  const std::size_t first_chunk_size = 256; // read first 256 bytes to check if binary
  const std::size_t chunk_size = 1024 * 1024; // read in 1 MB chunks
  char* chunk_data = new char[chunk_size];

  // Read first chunk to check if binary
  file.read(chunk_data, first_chunk_size);
  bool is_binary = std::find(chunk_data, chunk_data + first_chunk_size, '\0') != chunk_data + first_chunk_size;

  if (is_binary) {
    delete[] chunk_data;
    return;
  }

  std::streampos bytes_read = first_chunk_size;
  while (bytes_read < file_size) {
    const std::streampos remaining_bytes = file_size - bytes_read;
    const std::size_t bytes_to_read = static_cast<std::size_t>(std::min<std::streampos>(remaining_bytes, chunk_size));

    file.read(chunk_data, bytes_to_read);

    // Process the chunk here
    file_context ctx { filename.data(), chunk_data, bytes_to_read };
    hs_scan(database, chunk_data, bytes_to_read, 0, scratch, on_match, (void *)(&ctx));

    bytes_read += bytes_to_read;
  }

  delete[] chunk_data;
}

static inline int visit(const char *path) {
    DIR *dir = opendir(path);
    if (dir == NULL) {
        // perror("opendir");
        return -1;
    }

    std::vector<std::thread> visit_threads;

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
              visit_threads.push_back(std::thread([filename = std::string(filepath)]() {
                visit(filename.data());
              }));
            }
        }
        // Check if path is a regular file 
        else if (entry->d_type == DT_REG) {
          file_num++;
          struct stat fileStat;
          if (stat(filepath, &fileStat) == -1) {
              perror("Error");
              return 1;
          }
          const std::streampos file_size = fileStat.st_size;
          process_file(filename, file_size);
        }
    }

    for (auto& t : visit_threads) {
      t.join();
    }

    closedir(dir);
    return 0;
}

int main(int argc, char** argv) {
  std::ios_base::sync_with_stdio(false);

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

  // Set up the scratch space
  hs_error_t database_error = hs_alloc_scratch(database, &scratch);
  if (database_error != HS_SUCCESS)
  {
      fprintf(stderr, "Error allocating scratch space\n");
      hs_free_database(database);
      return 1;
  }

  if (visit(resolved_path) == -1) {
      return 1;
  }

  git_repository_free(repo);

  return 0;
}

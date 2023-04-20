#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <git2.h>
#include <hs/hs.h>

git_repository* repo = nullptr;
hs_database_t *database = NULL;
hs_scratch_t *scratch = NULL;

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
    auto start = to, end = to;
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
        // New file
        // Print filename
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << filename << "\n";
        current_file_num = file_num;
    }

    if (end > start) {
        // std::string_view line(&data[start], end - start);
        // std::lock_guard<std::mutex> lock(cout_mutex);
        // std::cout << line << "\n";
    }
    return 0;
}

void process_chunk(const std::string& filename, int chunk_num, const std::streampos file_size) {
  std::ifstream file(filename, std::ios::binary);
  if (!file) {
    std::cerr << "Failed to open file: " << filename << std::endl;
    return;
  }

  const std::streampos start_pos = chunk_num * CHUNK_SIZE;
  const std::streampos end_pos = std::min(start_pos + CHUNK_SIZE, file_size);

  file.seekg(start_pos);
  const std::size_t chunk_size = static_cast<std::size_t>(end_pos - start_pos);
  std::vector<char> chunk_data(chunk_size);

  file.read(chunk_data.data(), chunk_size);
  if (file.gcount() != chunk_size) {
    std::cerr << "Failed to read chunk " << chunk_num << " from file: " << filename << std::endl;
    return;
  }

  // Process the chunk here
  file_context ctx { filename.data(), chunk_data.data(), chunk_data.size() };
  hs_scan(database, chunk_data.data(), chunk_data.size(), 0, scratch, on_match, (void *)(&ctx));

  // Print the chunk number
  // std::lock_guard<std::mutex> lock(cout_mutex);
  // std::cout << "Processed chunk " << chunk_num << " (" << start_pos << "," << end_pos << ")" << std::endl;
}

int visit(const char *path) {
    DIR *dir = opendir(path);
    if (dir == NULL) {
        // perror("opendir");
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {

        // Construct path
        const size_t path_len = strlen(path);
        const size_t name_len = _D_EXACT_NAMLEN(entry);
        const size_t total_len = path_len + 1 + name_len + 1;
        char filepath[total_len];
        memcpy(filepath, path, path_len);
        filepath[path_len] = '/';
        memcpy(filepath + path_len + 1, entry->d_name, name_len);
        filepath[total_len - 1] = '\0';

        // Check if path is a directory
        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            if (!is_ignored(filepath))
            {
                if (visit(filepath) == -1) {
                    closedir(dir);
                    return -1;
                }
            }
        }
        // Check if path is a regular file 
        else if (entry->d_type == DT_REG) {

            if (!is_ignored(filepath))
            {
                std::string filename = filepath;
                file_num += 1;

                std::ifstream file(filename, std::ios::binary);
                if (!file) {
                  std::cerr << "Failed to open file: " << filename << std::endl;
                  return 1;
                }

                file.seekg(0, std::ios::end);
                const std::streampos file_size = file.tellg();
                file.seekg(0, std::ios::beg);

                const int num_chunks = static_cast<int>(((int)(file_size + CHUNK_SIZE) - 1) / CHUNK_SIZE);
                std::vector<std::thread> threads(num_chunks);

                for (int i = 0; i < num_chunks; ++i) {
                  threads[i] = std::thread(process_chunk, filename, i, file_size);
                }

                for (auto& thread : threads) {
                  thread.join();
                }
            }
        }
    }

    closedir(dir);
    return 0;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <filename>" << std::endl;
    return 1;
  }

  const char *path = argv[1];
  const char *pattern = argv[2];

  char resolved_path[PATH_MAX]; 
  realpath(path, resolved_path); 

  git_libgit2_init();
  if (git_repository_open(&repo, resolved_path) < 0) {
      // failed to open repository
  }

  hs_compile_error_t *compile_error = NULL;
  hs_error_t error_code = hs_compile(pattern, 0, HS_MODE_BLOCK, NULL, &database, &compile_error);
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

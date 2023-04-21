#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <filesystem>
#include <git2.h>
#include <hs/hs.h>
#include "concurrentqueue.h"

#include <immintrin.h>

#if defined(__AVX2__)
#define HAS_AVX2_SUPPORT 1
#else
#define HAS_AVX2_SUPPORT 0
#endif

moodycamel::ConcurrentQueue<std::string> queue;
moodycamel::ProducerToken ptok(queue);

std::atomic<bool> running{true};
std::atomic<std::size_t> n{0};
std::atomic<std::size_t> n_consumed{0};
git_repository* repo = nullptr;
hs_database_t *database = NULL;
hs_scratch_t *scratch = NULL;
std::mutex cout_mutex;

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

struct file_context {
    const char* data;
    std::size_t size;
    std::string& lines;
    std::size_t& current_line_number;
    const char** current_ptr;
};

std::atomic<std::size_t> matches{0};

#include <immintrin.h> // required for AVX2

#if 1 // HAS_AVX2_SUPPORT
std::size_t count_newlines(const char* start, const char* end) {
    const __m256i char_vector = _mm256_set1_epi8('\n');
    const char *str_ptr = start;

    if (end <= start) { return 0; }

    const std::size_t str_size = end - start;
    std::size_t i = 0;
    std::size_t local_n = 0;
    const std::size_t chunk_size = 64;

    // Align str_ptr to 32-byte boundary
    const std::size_t offset = (std::size_t)str_ptr % 32;
    str_ptr += (offset != 0) ? (32 - offset) : 0;

    // Prefetch the next cache line
    __builtin_prefetch(str_ptr + chunk_size);

    // Use aligned loads and prefetching
    while (i + chunk_size <= str_size)
    {
        // Prefetch the next cache line
        __builtin_prefetch(str_ptr + chunk_size * 2);

        const __m256i str_vector1 = _mm256_load_si256((__m256i *)(str_ptr + i));
        const __m256i str_vector2 = _mm256_load_si256((__m256i *)(str_ptr + i + 32));
        const __m256i cmp_result1 = _mm256_cmpeq_epi8(str_vector1, char_vector);
        const __m256i cmp_result2 = _mm256_cmpeq_epi8(str_vector2, char_vector);
        const unsigned int mask1 = _mm256_movemask_epi8(cmp_result1);
        const unsigned int mask2 = _mm256_movemask_epi8(cmp_result2);

        if (mask1 || mask2)
        {
            // Use the POPCNT instruction to count the number of set bits in the mask
            local_n += _mm_popcnt_u32(mask1) + _mm_popcnt_u32(mask2);
        }

        i += chunk_size;

        // Prefetch the next cache line
        __builtin_prefetch(str_ptr + i + chunk_size);
    }

    // Process remaining characters
    for (; i < str_size; i++)
    {
        local_n += (str_ptr[i] == '\n');
    }

  return local_n;
}
#else
std::size_t count_newlines(const char* start, const char* end) {
  std::size_t newline_count = 0;
  for (const char* p = start; p < end; p++) {
      if (*p == '\n') {
          newline_count++;
      }
  }
  return newline_count;
}
#endif

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
    std::size_t& current_line_number = fctx->current_line_number;
    const char** current_ptr = fctx->current_ptr;

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

      current_line_number += count_newlines(*current_ptr, fctx->data + start);
      *current_ptr = fctx->data + end;

      if (from > start && to > from && end > to && end > start) {
        lines += "\033[32m";
        lines += std::to_string(current_line_number);
        lines += "\033[0m";
        lines += ":";
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

  char* file_data = new char[file_size];

  // Read the entire file into a big buffer
  file.read(file_data, file_size);

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
  std::size_t current_line_number{1};
  const char* current_ptr{file_data};
  std::string lines{""};
  file_context ctx { file_data, static_cast<std::size_t>(file_size), lines, current_line_number, &current_ptr };
  if (hs_scan(database, file_data, file_size, 0, local_scratch, on_match, (void *)(&ctx)) == HS_SUCCESS)
  {
    if (!lines.empty())
    {
      std::lock_guard<std::mutex> lock{cout_mutex};
      std::cout << "\n" << filename << "\n";
      std::cout << lines;
    }
  }

  delete[] file_data;
  hs_free_scratch(local_scratch);
  return true;
}

static inline int visit(const char* path) {
    std::filesystem::directory_iterator iter(path);
    for (const auto& entry : iter) {
        std::string filepath = entry.path().string();

        // Check if path is a directory
        if (entry.is_directory()) {
            if (entry.path().filename() == "." || entry.path().filename() == "..") {
                continue;
            }
            if (!is_ignored(filepath.c_str())) {
                visit(filepath.c_str());
            }
        }
        // Check if path is a regular file 
        else if (entry.is_regular_file()) {
            queue.enqueue(ptok, std::move(filepath));
            n += 1;
        }
    }

    return 0;
}

bool visit_one()
{
  std::string filepath{};
  auto found = queue.try_dequeue_from_producer(ptok, filepath);
  if (found) {
    process_file(filepath);
    n_consumed += 1;
    return true;
  } else {
    return false;
  }
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

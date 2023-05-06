#pragma once
#include <argparse/argparse.hpp>
#include <atomic>
#include <chrono>
#include <concurrentqueue/concurrentqueue.h>
#include <fcntl.h>
#include <file_search_options.hpp>
#include <filesystem>
#include <fmt/color.h>
#include <fmt/format.h>
#include <fstream>
#include <git2.h>
#include <hs/hs.h>
#include <is_binary.hpp>
#include <limits>
#include <numeric>
#include <set>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

class file_search {
public:
  file_search(argparse::ArgumentParser &program);
  ~file_search();

  void run(std::filesystem::path path);

private:
  struct file_context {
    std::atomic<size_t> &number_of_matches;
    std::set<std::pair<unsigned long long, unsigned long long>> &matches;
    hs_scratch *local_scratch;
  };

  struct line_context {
    const char *data;
    std::string &lines;
    const char **current_ptr;
  };

private:
  void compile_hs_database(std::string &pattern);

  bool mmap_and_scan(std::string &&filename);

  static int on_match(unsigned int id, unsigned long long from,
                      unsigned long long to, unsigned int flags, void *ctx);

  static int print_match_in_red_color(unsigned int id, unsigned long long from,
                                      unsigned long long to, unsigned int flags,
                                      void *ctx);

  void process_matches(const char *filename, char *buffer,
                       std::size_t bytes_read, file_context &ctx,
                       std::size_t &current_line_number, std::string &lines,
                       bool print_filename = true);

private:
  constexpr static inline std::size_t TYPICAL_FILESYSTEM_BLOCK_SIZE = 4096;
  constexpr static inline std::size_t FILE_CHUNK_SIZE =
      16 * TYPICAL_FILESYSTEM_BLOCK_SIZE;

private:
  hs_database_t *database = NULL;
  hs_scratch_t *scratch = NULL;
  std::vector<hs_scratch *> thread_local_scratch;
  std::vector<hs_scratch *> thread_local_scratch_per_line;
  file_search_options options;
};

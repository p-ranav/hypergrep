#pragma once
#include <argparse/argparse.hpp>
#include <atomic>
#include <chrono>
#include <concurrentqueue/concurrentqueue.h>
#include <fcntl.h>
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

class search {
public:
  search(argparse::ArgumentParser &program);
  ~search();

  void search_file(std::filesystem::path path);

  void search_directory(std::filesystem::path path);

private:
  struct file_context {
    std::atomic<size_t> &number_of_matches;
    std::set<std::pair<unsigned long long, unsigned long long>> &matches;
    hs_scratch *local_scratch;
    bool option_print_only_filenames;
  };

  struct line_context {
    const char *data;
    std::string &lines;
    const char **current_ptr;
  };

  struct mmap_context {
    char *start;
    std::size_t size;
  };

  struct filter_context {
    bool result{false};
  };

private:
  // Compile the HyperScan database for search
  void compile_hs_database(std::string &pattern);

  static int on_match(unsigned int id, unsigned long long from,
                      unsigned long long to, unsigned int flags, void *ctx);

  static int print_match_in_red_color(unsigned int id, unsigned long long from,
                                      unsigned long long to, unsigned int flags,
                                      void *ctx);

  void process_matches(const char *filename, char *buffer,
                       std::size_t bytes_read, file_context &ctx,
                       std::size_t &current_line_number, std::string &lines,
                       bool print_filename = true);

  bool process_file(std::string &&filename, std::size_t i, char *buffer,
                    std::string &lines);

  bool visit_one(const std::size_t i, char *buffer, std::string &lines);

  bool search_submodules(const char *dir, git_repository *this_repo);

  bool visit_git_index(const std::filesystem::path &dir, git_index *index);

  bool visit_git_repo(const std::filesystem::path &dir,
                      git_repository *repo = nullptr);

  void visit(const std::filesystem::path &path);

private:
  static int on_match_in_mmap(unsigned int id, unsigned long long from,
                              unsigned long long to, unsigned int flags,
                              void *ctx);

  bool process_file_mmap(std::string &&filename);

private:
  bool construct_file_filtering_hs_database();

  static int on_file_filter_match(unsigned int id, unsigned long long from,
                                  unsigned long long to, unsigned int flags,
                                  void *ctx);

  bool filter_file(const char *path);

private:
  constexpr static inline std::size_t TYPICAL_FILESYSTEM_BLOCK_SIZE = 4096;
  constexpr static inline std::size_t FILE_CHUNK_SIZE =
      16 * TYPICAL_FILESYSTEM_BLOCK_SIZE;

private:
  moodycamel::ConcurrentQueue<std::string> queue;
  moodycamel::ProducerToken ptok{queue};

  bool is_stdout{true};

  std::atomic<bool> running{true};
  std::atomic<std::size_t> num_files_enqueued{0};
  std::atomic<std::size_t> num_files_dequeued{0};

  hs_database_t *database = NULL;
  hs_scratch_t *scratch = NULL;
  std::vector<hs_scratch *> thread_local_scratch;
  std::vector<hs_scratch *> thread_local_scratch_per_line;
  bool option_show_line_numbers{false};
  bool option_ignore_case{false};
  bool option_print_only_filenames{false};
  bool option_count_matching_lines{false};
  bool option_exclude_submodules{false};
  std::size_t option_num_threads{0};
  bool option_filter_files{false};
  std::string option_filter_file_pattern{};
  hs_database_t *file_filter_database = NULL;
  hs_scratch_t *file_filter_scratch = NULL;
};

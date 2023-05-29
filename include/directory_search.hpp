#pragma once
#include <argparse/argparse.hpp>
#include <atomic>
#include <chrono>
#include <concurrentqueue/concurrentqueue.h>
#include <constants.hpp>
#include <directory_search_options.hpp>
#include <dirent.h>
#include <fcntl.h>
#include <file_search.hpp>
#include <filesystem>
#include <fmt/color.h>
#include <fmt/format.h>
#include <fstream>
#include <git2.h>
#include <git_index_search.hpp>
#include <hs/hs.h>
#include <is_binary.hpp>
#include <limits>
#include <match_handler.hpp>
#include <numeric>
#include <size_to_bytes.hpp>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

class directory_search {
public:
  directory_search(std::string& pattern,
                   const std::filesystem::path &path,
                   argparse::ArgumentParser &program);
  ~directory_search();
  void run(std::filesystem::path path);

private:
  struct filter_context {
    bool result{false};
  };

private:
  // Compile the HyperScan database for search
  void compile_hs_database(std::string &pattern);

  bool process_file(std::string &&filename, hs_scratch_t *local_scratch,
                    char *buffer, std::string &lines);

  bool try_dequeue_and_process_path(moodycamel::ConsumerToken& ctok, hs_scratch_t *local_scratch, char *buffer,
                                    std::string &lines);

  bool construct_file_filtering_hs_database();

  static int on_file_filter_match(unsigned int id, unsigned long long from,
                                  unsigned long long to, unsigned int flags,
                                  void *ctx);

  bool filter_file(const char *path, hs_scratch* local_file_filter_scratch);

  void search_thread_function();

  void visit_directory_and_enqueue(moodycamel::ProducerToken& ptok, std::string directory, hs_scratch* local_file_filter_scratch);

private:
  std::filesystem::path search_path;

  // If false, hypergrep will instead simply print the files
  // that _will_ be searched
  bool perform_search{true};
  bool compile_pattern_as_literal{false};

  // Directory traversal 
  moodycamel::ConcurrentQueue<std::string> subdirectories;
  std::atomic<std::size_t> num_dirs_enqueued{0};

  moodycamel::ConcurrentQueue<std::string> queue;

  std::atomic<bool> running{true};
  std::atomic<std::size_t> num_files_enqueued{0};
  std::atomic<std::size_t> num_files_dequeued{0};

  hs_database_t *database = NULL;
  hs_scratch_t *scratch = NULL;
  hs_database_t *file_filter_database = NULL;
  hs_scratch_t *file_filter_scratch = NULL;
  directory_search_options options;

  // Optimizations for large files
  moodycamel::ConcurrentQueue<std::string> large_file_backlog;
  std::atomic<std::size_t> num_large_files_enqueued{0};

  // Optimizations for git repos
  moodycamel::ConcurrentQueue<std::string> git_repo_paths;
  std::atomic<std::size_t> num_git_repos_enqueued{0};
};

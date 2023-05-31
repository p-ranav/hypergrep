#pragma once
#include <argparse/argparse.hpp>
#include <atomic>
#include <chrono>
#include <compiler.hpp>
#include <concurrentqueue/concurrentqueue.h>
#include <constants.hpp>
#include <search_options.hpp>
#include <dirent.h>
#include <fcntl.h>
#include <file_filter.hpp>
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
  directory_search(std::string &pattern, const std::filesystem::path &path,
                   argparse::ArgumentParser &program);
  ~directory_search();
  void run(std::filesystem::path path);

private:
  bool process_file(std::string &&filename, hs_scratch_t *local_scratch,
                    char *buffer, std::string &lines);

  bool try_dequeue_and_process_path(moodycamel::ConsumerToken &ctok,
                                    hs_scratch_t *local_scratch, char *buffer,
                                    std::string &lines);

  void search_thread_function();

  void visit_directory_and_enqueue(moodycamel::ProducerToken &ptok,
                                   std::string directory,
                                   hs_scratch *local_file_filter_scratch);

private:
  std::filesystem::path search_path;

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
  // If the filter pattern starts with '!'
  // then negate the result of the filter
  bool negate_filter{false};

  search_options options;

  // Optimizations for large files
  struct large_file {
    std::string path;
    std::size_t size;
  };
  moodycamel::ConcurrentQueue<large_file> large_file_backlog;
  std::atomic<std::size_t> num_large_files_enqueued{0};

  // Optimizations for git repos
  moodycamel::ConcurrentQueue<std::string> git_repo_paths;
  std::atomic<std::size_t> num_git_repos_enqueued{0};
};

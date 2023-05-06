#pragma once
#include <argparse/argparse.hpp>
#include <atomic>
#include <chrono>
#include <concurrentqueue/concurrentqueue.h>
#include <constants.hpp>
#include <directory_search_options.hpp>
#include <fcntl.h>
#include <filesystem>
#include <fmt/color.h>
#include <fmt/format.h>
#include <fstream>
#include <git2.h>
#include <hs/hs.h>
#include <is_binary.hpp>
#include <limits>
#include <match_handler.hpp>
#include <numeric>
#include <set>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

class directory_search {
public:
  directory_search(argparse::ArgumentParser &program);
  ~directory_search();
  void run(std::filesystem::path path);

private:
  struct filter_context {
    bool result{false};
  };

private:
  // Compile the HyperScan database for search
  void compile_hs_database(std::string &pattern);

  bool process_file(std::string &&filename, std::size_t i, char *buffer,
                    std::string &lines);

  bool search_submodules(const char *dir, git_repository *this_repo);

  bool visit_git_index(const std::filesystem::path &dir, git_index *index);

  bool visit_git_repo(const std::filesystem::path &dir,
                      git_repository *repo = nullptr);

  bool try_dequeue_and_process_path(const std::size_t i, char *buffer,
                                    std::string &lines);

  bool construct_file_filtering_hs_database();

  static int on_file_filter_match(unsigned int id, unsigned long long from,
                                  unsigned long long to, unsigned int flags,
                                  void *ctx);

  bool filter_file(const char *path);

  void visit_directory_and_enqueue(const std::filesystem::path &path);

private:
  moodycamel::ConcurrentQueue<std::string> queue;
  moodycamel::ProducerToken ptok{queue};

  std::atomic<bool> running{true};
  std::atomic<std::size_t> num_files_enqueued{0};
  std::atomic<std::size_t> num_files_dequeued{0};

  hs_database_t *database = NULL;
  hs_scratch_t *scratch = NULL;
  hs_database_t *file_filter_database = NULL;
  hs_scratch_t *file_filter_scratch = NULL;
  std::vector<hs_scratch *> thread_local_scratch;
  std::vector<hs_scratch *> thread_local_scratch_per_line;
  directory_search_options options;
};

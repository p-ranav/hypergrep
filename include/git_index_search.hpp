#pragma once
#include <argparse/argparse.hpp>
#include <atomic>
#include <chrono>
#include <concurrentqueue/concurrentqueue.h>
#include <constants.hpp>
#include <directory_search_options.hpp>
#include <fcntl.h>
#include <file_search.hpp>
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
#include <size_to_bytes.hpp>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

class git_index_search {
public:
  git_index_search(argparse::ArgumentParser &program);
  ~git_index_search();
  void run(std::filesystem::path path);

private:
  struct filter_context {
    bool result{false};
  };

private:
  // Compile the HyperScan database for search
  void compile_hs_database(std::string &pattern);

  bool process_file(const char *filename, hs_scratch_t *local_scratch,
                    char *buffer, std::string &lines);

  bool search_submodules(const char *dir, git_repository *this_repo);

  bool visit_git_index(const std::filesystem::path &dir, git_index *index);

  bool visit_git_repo(const std::filesystem::path &dir,
                      git_repository *repo = nullptr);

  bool try_dequeue_and_process_path(hs_scratch_t *local_scratch, char *buffer,
                                    std::string &lines);

  bool construct_file_filtering_hs_database();

  static int on_file_filter_match(unsigned int id, unsigned long long from,
                                  unsigned long long to, unsigned int flags,
                                  void *ctx);

  bool filter_file(const char *path);

  void visit_directory_and_enqueue(const std::filesystem::path &path);

private:
  moodycamel::ConcurrentQueue<const char *> queue;
  moodycamel::ProducerToken ptok{queue};

  std::atomic<bool> running{true};
  std::atomic<std::size_t> num_files_enqueued{0};
  std::atomic<std::size_t> num_files_dequeued{0};

  hs_database_t *database = NULL;
  hs_scratch_t *scratch = NULL;
  hs_database_t *file_filter_database = NULL;
  hs_scratch_t *file_filter_scratch = NULL;
  directory_search_options options;

  std::vector<git_repository *> garbage_collect_repo;
  std::vector<git_index *> garbage_collect_index;
  std::vector<git_index_iterator *> garbage_collect_index_iterator;
};

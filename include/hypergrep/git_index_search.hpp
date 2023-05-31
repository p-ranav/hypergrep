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
#include <hypergrep/compiler.hpp>
#include <hypergrep/constants.hpp>
#include <hypergrep/file_filter.hpp>
#include <hypergrep/file_search.hpp>
#include <hypergrep/is_binary.hpp>
#include <hypergrep/match_handler.hpp>
#include <hypergrep/search_options.hpp>
#include <hypergrep/size_to_bytes.hpp>
#include <limits>
#include <numeric>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

class git_index_search {
public:
  git_index_search(std::string &pattern, const std::filesystem::path &path,
                   argparse::ArgumentParser &program);
  git_index_search(hs_database_t *database, hs_scratch_t *scratch,
                   hs_database_t *file_filter_database,
                   hs_scratch_t *file_filter_scratch,
                   const search_options &options,
                   const std::filesystem::path &path);
  ~git_index_search();
  void run(std::filesystem::path path);

private:
  bool process_file(const char *filename, hs_scratch_t *local_scratch,
                    char *buffer, std::string &lines);

  bool search_submodules(const char *dir, git_repository *this_repo);

  bool visit_git_index(const std::filesystem::path &dir, git_index *index);

  bool visit_git_repo(const std::filesystem::path &dir,
                      git_repository *repo = nullptr);

  bool try_dequeue_and_process_path(hs_scratch_t *local_scratch, char *buffer,
                                    std::string &lines);

  void search_thread_function();

private:
  static inline bool libgit2_initialized{false};
  std::filesystem::path basepath{};

  moodycamel::ConcurrentQueue<const char *> queue;
  moodycamel::ProducerToken ptok{queue};

  std::atomic<bool> running{true};
  std::atomic<std::size_t> num_files_enqueued{0};
  std::atomic<std::size_t> num_files_dequeued{0};

  bool non_owning_database{false};
  hs_database_t *database = NULL;
  hs_scratch_t *scratch = NULL;
  hs_database_t *file_filter_database = NULL;
  hs_scratch_t *file_filter_scratch = NULL;
  // If the filter pattern starts with '!'
  // then negate the result of the filter
  bool negate_filter{false};

  search_options options;

  std::vector<git_repository *> garbage_collect_repo;
  std::vector<git_index *> garbage_collect_index;
  std::vector<git_index_iterator *> garbage_collect_index_iterator;

  // Backlog
  std::vector<std::filesystem::path> submodule_paths;
};

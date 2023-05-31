#pragma once
#include <argparse/argparse.hpp>
#include <atomic>
#include <chrono>
#include <compiler.hpp>
#include <concurrentqueue/concurrentqueue.h>
#include <constants.hpp>
#include <fcntl.h>
#include <search_options.hpp>
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
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

class file_search {
public:
  file_search(std::string &pattern, argparse::ArgumentParser &program);
  file_search(hs_database_t *database, hs_scratch_t *scratch,
              const search_options &options);
  ~file_search();

  void run(std::filesystem::path path,
           std::optional<std::size_t> maybe_file_size = {});
  bool scan_line(std::string &line, std::size_t &current_line_number,
                 bool &break_loop);

private:
  bool mmap_and_scan(std::string &&filename,
                     std::optional<std::size_t> maybe_file_size = {});

private:
  bool non_owning_database{false};

  hs_database_t *database = NULL;
  hs_scratch_t *scratch = NULL;
  hs_database_t *file_filter_database = NULL;
  hs_scratch_t *file_filter_scratch = NULL;
  // If the filter pattern starts with '!'
  // then negate the result of the filter
  bool negate_filter{false};

  std::vector<hs_scratch *> thread_local_scratch;
  std::vector<hs_scratch *> thread_local_scratch_per_line;
  search_options options;
};

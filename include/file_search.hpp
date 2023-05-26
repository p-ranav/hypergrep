#pragma once
#include <argparse/argparse.hpp>
#include <atomic>
#include <chrono>
#include <concurrentqueue/concurrentqueue.h>
#include <constants.hpp>
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
#include <match_handler.hpp>
#include <numeric>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

class file_search {
public:
  file_search(argparse::ArgumentParser &program);
  file_search(hs_database_t *database, hs_scratch_t *scratch,
              const file_search_options &options);
  ~file_search();

  void run(std::filesystem::path path);
  bool scan_line(std::string &line, std::size_t &current_line_number,
                 bool &break_loop);

private:
  void compile_hs_database(std::string &pattern);

  bool mmap_and_scan(std::string &&filename);

private:
  bool non_owning_database{false};
  bool compile_pattern_as_literal{false};

  hs_database_t *database = NULL;
  hs_scratch_t *scratch = NULL;
  std::vector<hs_scratch *> thread_local_scratch;
  std::vector<hs_scratch *> thread_local_scratch_per_line;
  file_search_options options;
};

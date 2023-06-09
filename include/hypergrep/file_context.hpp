#pragma once
#include <atomic>
#include <cerrno>
#include <hs/hs.h>
#include <vector>

struct file_context {
  std::atomic<size_t> &number_of_matches;
  std::vector<std::pair<unsigned long long, unsigned long long>> &matches;
  bool option_print_only_filenames;
};

#pragma once
#include <atomic>
#include <hs/hs.h>
#include <set>

struct file_context {
  std::atomic<size_t> &number_of_matches;
  std::set<std::pair<unsigned long long, unsigned long long>> &matches;
  bool option_print_only_filenames;
};

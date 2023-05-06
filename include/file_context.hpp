#pragma once
#include <set>
#include <atomic>
#include <hs/hs.h>

struct file_context {
  std::atomic<size_t> &number_of_matches;
  std::set<std::pair<unsigned long long, unsigned long long>> &matches;
  hs_scratch *local_scratch;
  bool option_print_only_filenames;  
};

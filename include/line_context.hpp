#pragma once
#include <set>

struct line_context {
  std::set<std::pair<unsigned long long, unsigned long long>> &matches;
};

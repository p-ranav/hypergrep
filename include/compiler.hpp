#pragma once
#include <fmt/core.h>
#include <hs/hs.h>
#include <vector>
#include <string>

struct search_options;

void compile_hs_database(hs_database** database, hs_scratch** scratch, search_options& options, const std::vector<std::string> &pattern_list);
#pragma once
#include <hs/hs.h>
#include <cstdio>
#include <cstring>
#include <search_options.hpp>

struct filter_context {
  bool result{false};
};

bool construct_file_filtering_hs_database(hs_database** file_filter_database, hs_scratch** file_filter_scratch, search_options& options);

int on_file_filter_match(unsigned int id, unsigned long long from,
                                unsigned long long to, unsigned int flags,
                                void *ctx);

bool filter_file(const char *path, hs_database* file_filter_database, hs_scratch *local_file_filter_scratch, const bool& negate_filter);
#pragma once
#include <hs/hs.h>
#include <cstdio>
#include <cstring>

struct filter_context {
  bool result{false};
};

template <typename Options>
bool construct_file_filtering_hs_database(hs_database** file_filter_database, hs_scratch** file_filter_scratch, 
    Options& options, bool& negate_filter) {

  negate_filter = options.filter_file_pattern[0] == '!';
  if (negate_filter) {
    options.filter_file_pattern = options.filter_file_pattern.substr(1);
  }

  hs_compile_error_t *compile_error = NULL;
  auto error_code =
      hs_compile(options.filter_file_pattern.c_str(), HS_FLAG_UTF8,
                 HS_MODE_BLOCK, NULL, file_filter_database, &compile_error);
  if (error_code != HS_SUCCESS) {
    fprintf(stderr, "Error compiling pattern: %s\n", compile_error->message);
    hs_free_compile_error(compile_error);
    return false;
  }

  auto database_error =
      hs_alloc_scratch(*file_filter_database, file_filter_scratch);
  if (database_error != HS_SUCCESS) {
    fprintf(stderr, "Error allocating scratch space\n");
    hs_free_database(*file_filter_database);
    return false;
  }

  return true;
}

int on_file_filter_match(unsigned int id, unsigned long long from,
                                unsigned long long to, unsigned int flags,
                                void *ctx);

bool filter_file(const char *path, hs_database* file_filter_database, hs_scratch *local_file_filter_scratch, bool& negate_filter);
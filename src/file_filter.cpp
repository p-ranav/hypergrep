#include <file_filter.hpp>

bool construct_file_filtering_hs_database(hs_database** file_filter_database, hs_scratch** file_filter_scratch, search_options& options) {

  options.negate_filter = options.filter_file_pattern[0] == '!';
  if (options.negate_filter) {
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
                                void *ctx) {
  filter_context *fctx = (filter_context *)(ctx);
  fctx->result = true;
  return HS_SUCCESS;
}

bool filter_file(const char *path, hs_database* file_filter_database, hs_scratch *local_file_filter_scratch, const bool& negate_filter) {

  // Result of `true` means that the file will be searched
  // Result of `false` means that the file will be ignored
  //
  // If negate_filter is `true`, the result is flipped
  bool result{false};

  filter_context ctx{false};
  if (hs_scan(file_filter_database, path, strlen(path), 0,
              local_file_filter_scratch, on_file_filter_match,
              (void *)(&ctx)) != HS_SUCCESS) {
    result = true;
  } else {
    result = ctx.result;
  }

  if (negate_filter) {
    result = !result;
  }

  return result;
}
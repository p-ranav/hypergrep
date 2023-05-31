#include <file_filter.hpp>

int on_file_filter_match(unsigned int id, unsigned long long from,
                                unsigned long long to, unsigned int flags,
                                void *ctx) {
  filter_context *fctx = (filter_context *)(ctx);
  fctx->result = true;
  return HS_SUCCESS;
}

bool filter_file(const char *path, hs_database* file_filter_database, hs_scratch *local_file_filter_scratch, bool& negate_filter) {

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
#pragma once
#include <fmt/core.h>
#include <hs/hs.h>
#include <vector>
#include <string>

template <typename Options>
void compile_hs_database(hs_database** database, hs_scratch** scratch, 
    Options& options,
    const std::vector<std::string> &pattern_list, bool compile_pattern_as_literal) {

  hs_error_t error_code;
  hs_compile_error_t *compile_error = NULL;

  if (pattern_list.size() == 1) {
    const auto& pattern = pattern_list[0];

    if (compile_pattern_as_literal) {
      error_code = hs_compile_lit(
          pattern.data(),
          (options.ignore_case ? HS_FLAG_CASELESS : 0) |
              (options.is_stdout || options.print_only_matching_parts ||
                      options.show_column_numbers || options.show_byte_offset
                  ? HS_FLAG_SOM_LEFTMOST
                  : 0),
          pattern.size(), HS_MODE_BLOCK, NULL, database, &compile_error);
    } else {
      error_code = hs_compile(
          pattern.data(),
          (options.ignore_case ? HS_FLAG_CASELESS : 0) | HS_FLAG_UTF8 |
              (options.use_ucp ? HS_FLAG_UCP : 0) |
              (options.is_stdout || options.print_only_matching_parts ||
                      options.show_column_numbers || options.show_byte_offset
                  ? HS_FLAG_SOM_LEFTMOST
                  : 0),
          HS_MODE_BLOCK, NULL, database, &compile_error);
    }
  }
  else {
    // Compile multiple patterns
    // using hs_compile_multi

    // Search patterns
    std::vector<const char*> pattern_list_c;
    pattern_list_c.reserve(pattern_list.size());
    for (const std::string& str : pattern_list) {
      pattern_list_c.push_back(str.data());
    }

    // Search flags
    const auto flag = (options.ignore_case ? HS_FLAG_CASELESS : 0) | 
          (compile_pattern_as_literal ? 0 : HS_FLAG_UTF8) |
          (!compile_pattern_as_literal && options.use_ucp ? HS_FLAG_UCP : 0) |
          (options.is_stdout || options.print_only_matching_parts ||
                  options.show_column_numbers || options.show_byte_offset
              ? HS_FLAG_SOM_LEFTMOST
              : 0);
    std::vector<unsigned int> flags;
    flags.reserve(pattern_list.size());
    for (std::size_t i = 0; i < pattern_list.size(); ++i) {
      flags.push_back(flag);
    }

    if (compile_pattern_as_literal) {

      std::vector<size_t> lens;
      lens.reserve(pattern_list.size());
      for (const std::string& str : pattern_list) {
          lens.push_back(str.size());
      }

      error_code = hs_compile_lit_multi(
          pattern_list_c.data(),
          flags.data(),
          NULL, // list of IDs - NULL means all zero
          lens.data(), 
          pattern_list.size(), HS_MODE_BLOCK, NULL, database, &compile_error);
    }
    else {
      error_code = hs_compile_multi(
        pattern_list_c.data(),
        flags.data(),
        NULL, // list of IDs - NULL means all zero
        pattern_list.size(),
        HS_MODE_BLOCK, NULL, database, &compile_error);
    }
  }

  if (error_code != HS_SUCCESS) {
    throw std::runtime_error(std::string{"Error compiling pattern: "} +
                            compile_error->message);
  }

  auto database_error = hs_alloc_scratch(*database, scratch);
  if (database_error != HS_SUCCESS) {
    throw std::runtime_error("Error allocating scratch space");
  }
}
#include <hypergrep/compiler.hpp>
#include <hypergrep/cpu_features.hpp>
#include <hypergrep/search_options.hpp>

unsigned int get_cpu_features_flag() {
  if (has_avx512vbmi_support()) {
    return HS_CPU_FEATURES_AVX512VBMI;
  } else if (has_avx512_support()) {
    return HS_CPU_FEATURES_AVX512;
  } else if (has_avx2_support()) {
    return HS_CPU_FEATURES_AVX2;
  } else {
    return 0;
  }
}

void compile_hs_database(hs_database **database, hs_scratch **scratch,
                         search_options &options,
                         const std::vector<std::string> &pattern_list) {

  hs_error_t error_code;
  hs_compile_error_t *compile_error = NULL;

  static const auto cpu_features_flag = get_cpu_features_flag();

  if (pattern_list.size() == 1) {
    const auto &pattern = pattern_list[0];

    if (options.compile_pattern_as_literal) {
      error_code = hs_compile_lit(
          pattern.data(),
          (options.ignore_case ? HS_FLAG_CASELESS : 0) |
              (options.is_stdout || options.print_only_matching_parts ||
                       options.show_column_numbers || options.show_byte_offset
                   ? HS_FLAG_SOM_LEFTMOST
                   : 0) |
              cpu_features_flag,
          pattern.size(), HS_MODE_BLOCK, NULL, database, &compile_error);
    } else {
      error_code = hs_compile(
          pattern.data(),
          (options.ignore_case ? HS_FLAG_CASELESS : 0) | HS_FLAG_UTF8 |
              (options.use_ucp ? HS_FLAG_UCP : 0) |
              (options.is_stdout || options.print_only_matching_parts ||
                       options.show_column_numbers || options.show_byte_offset
                   ? HS_FLAG_SOM_LEFTMOST
                   : 0) |
              cpu_features_flag,
          HS_MODE_BLOCK, NULL, database, &compile_error);
    }
  } else {
    // Compile multiple patterns
    // using hs_compile_multi

    // Search patterns
    std::vector<const char *> pattern_list_c;
    pattern_list_c.reserve(pattern_list.size());
    for (const std::string &str : pattern_list) {
      pattern_list_c.push_back(str.data());
    }

    // Search flags
    const auto flag =
        (options.ignore_case ? HS_FLAG_CASELESS : 0) |
        (options.compile_pattern_as_literal ? 0 : HS_FLAG_UTF8) |
        (!options.compile_pattern_as_literal && options.use_ucp ? HS_FLAG_UCP
                                                                : 0) |
        (options.is_stdout || options.print_only_matching_parts ||
                 options.show_column_numbers || options.show_byte_offset
             ? HS_FLAG_SOM_LEFTMOST
             : 0) |
        cpu_features_flag;
    std::vector<unsigned int> flags;
    flags.reserve(pattern_list.size());
    for (std::size_t i = 0; i < pattern_list.size(); ++i) {
      flags.push_back(flag);
    }

    if (options.compile_pattern_as_literal) {

      std::vector<size_t> lens;
      lens.reserve(pattern_list.size());
      for (const std::string &str : pattern_list) {
        lens.push_back(str.size());
      }

      error_code =
          hs_compile_lit_multi(pattern_list_c.data(), flags.data(),
                               NULL, // list of IDs - NULL means all zero
                               lens.data(), pattern_list.size(), HS_MODE_BLOCK,
                               NULL, database, &compile_error) |
          cpu_features_flag;
    } else {
      error_code = hs_compile_multi(pattern_list_c.data(), flags.data(),
                                    NULL, // list of IDs - NULL means all zero
                                    pattern_list.size(), HS_MODE_BLOCK, NULL,
                                    database, &compile_error) |
                   cpu_features_flag;
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
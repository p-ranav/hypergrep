#pragma once
#include <argparse/argparse.hpp>
#include <cstdint>
#include <hypergrep/file_filter.hpp>
#include <hypergrep/size_to_bytes.hpp>
#include <optional>
#include <string>
#include <unistd.h>

struct search_options {
  bool perform_search{true};
  bool is_stdout{true};
  bool show_line_numbers{false};
  bool show_column_numbers{false};
  bool show_byte_offset{false};
  bool ignore_case{false};
  bool print_only_filenames{false};
  bool count_matching_lines{false};
  bool count_matches{false};
  bool count_include_zeros{false};
  bool use_ucp{false};
  bool exclude_submodules{false};
  std::size_t num_threads{0};
  bool filter_files{false};
  std::string filter_file_pattern{};
  bool negate_filter{false};
  std::optional<unsigned long long> max_file_size{};
  bool search_hidden_files{false};
  bool print_only_matching_parts{false};
  std::optional<std::size_t> max_column_limit{};
  bool print_filenames{true};
  bool search_binary_files{false};
  bool ignore_gitindex{false};
  bool compile_pattern_as_literal{false};
};

void initialize_search(std::string &pattern, argparse::ArgumentParser &program,
                       search_options &options, hs_database **database,
                       hs_scratch **scratch, hs_database **file_filter_database,
                       hs_scratch **file_filter_scratch);
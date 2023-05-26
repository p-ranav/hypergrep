#pragma once
#include <cstdint>
#include <optional>
#include <string>

struct directory_search_options {
  bool is_stdout{true};
  bool show_line_numbers{false};
  bool show_column_numbers{false};
  bool show_byte_offset{false};
  bool ignore_case{false};
  bool print_only_filenames{false};
  bool count_matching_lines{false};
  bool count_matches{false};
  bool use_ucp{false};
  bool exclude_submodules{false};
  std::size_t num_threads{0};
  bool filter_files{false};
  std::string filter_file_pattern{};
  std::optional<unsigned long long> max_file_size{};
  bool search_hidden_files{false};
  bool print_only_matching_parts{false};
  std::optional<std::size_t> max_column_limit{};
  bool print_filenames{true};
  bool search_binary_files{false};
};

#pragma once
#include <optional>

struct directory_search_options {
  bool is_stdout{true};
  bool show_line_numbers{false};
  bool ignore_case{false};
  bool print_only_filenames{false};
  bool count_matching_lines{false};
  bool use_ucp{false};
  bool exclude_submodules{false};
  std::size_t num_threads{0};
  bool filter_files{false};
  std::string filter_file_pattern{};
  std::optional<unsigned long long> max_file_size{};
  bool search_hidden_files{false};
  bool print_only_matching_parts{false};
  std::optional<std::size_t> max_column_limit{};
};

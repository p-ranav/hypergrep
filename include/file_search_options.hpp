#pragma once
#include <optional>

struct file_search_options {
  bool is_stdout{true};
  bool show_line_numbers{false};
  bool ignore_case{false};
  bool count_matching_lines{false};
  bool use_ucp{false};
  std::size_t num_threads{0};
  bool print_filename{false};
  bool print_only_matching_parts{false};
  std::optional<std::size_t> max_column_limit{};
};

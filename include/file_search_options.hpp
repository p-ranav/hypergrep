#pragma once

struct file_search_options {
  bool is_stdout{true};
  bool show_line_numbers{false};
  bool ignore_case{false};
  bool count_matching_lines{false};
  bool use_ucp{false};
  std::size_t num_threads{0};
  bool print_filename{false};
};

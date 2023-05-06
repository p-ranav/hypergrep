#pragma once

struct file_search_options {
  bool is_stdout{true};
  bool show_line_numbers{false};
  bool ignore_case{false};
  bool count_matching_lines{false};
  std::size_t num_threads{0};
};

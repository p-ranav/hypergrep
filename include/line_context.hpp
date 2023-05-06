#pragma once
#include <string>

struct line_context {
  const char *data;
  std::string &lines;
  const char **current_ptr;
};

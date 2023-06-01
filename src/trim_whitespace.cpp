#include <hypergrep/trim_whitespace.hpp>

std::string_view ltrim(const std::string_view &s) {
  const auto start = s.find_first_not_of(WHITESPACE);
  return (start == std::string_view::npos) ? "" : s.substr(start);
}

std::string_view rtrim(const std::string_view &s) {
  const auto end = s.find_last_not_of(WHITESPACE);
  return (end == std::string_view::npos) ? "" : s.substr(0, end + 1);
}

std::string_view trim(const std::string_view &s) { return rtrim(ltrim(s)); }
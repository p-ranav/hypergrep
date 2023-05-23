#pragma once
#include <algorithm>
#include <file_context.hpp>
#include <fmt/color.h>
#include <fmt/format.h>
#include <optional>
#include <string_view>
#include <vector>

int on_match(unsigned int id, unsigned long long from, unsigned long long to,
             unsigned int flags, void *ctx);

int print_match_in_red_color(unsigned int id, unsigned long long from,
                             unsigned long long to, unsigned int flags,
                             void *ctx);

std::size_t process_matches(
    const char *filename, char *buffer, std::size_t bytes_read,
    std::vector<std::pair<unsigned long long, unsigned long long>> &matches,
    std::size_t &current_line_number, std::string &lines, bool print_filename,
    bool is_stdout, bool show_line_numbers, bool print_only_matching_parts,
    const std::optional<std::size_t> &max_column_limit);

std::size_t process_matches_nocolor_nostdout(
    const char *filename, char *buffer, std::size_t bytes_read,
    std::vector<std::pair<unsigned long long, unsigned long long>> &matches,
    std::size_t &current_line_number, std::string &lines, bool print_filename,
    bool is_stdout, bool show_line_numbers, bool print_only_matching_parts,
    const std::optional<std::size_t> &max_column_limit);
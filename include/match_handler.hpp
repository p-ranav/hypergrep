#pragma once
#include <algorithm>
#include <file_context.hpp>
#include <fmt/color.h>
#include <fmt/format.h>
#include <string_view>

int on_match(unsigned int id, unsigned long long from, unsigned long long to,
             unsigned int flags, void *ctx);

int print_match_in_red_color(unsigned int id, unsigned long long from,
                             unsigned long long to, unsigned int flags,
                             void *ctx);

void process_matches(const char *filename,
                     char *buffer, std::size_t bytes_read, file_context &ctx,
                     std::size_t &current_line_number, std::string &lines,
                     bool print_filename, bool is_stdout,
                     bool show_line_numbers);

#include <match_handler.hpp>

int on_match(unsigned int id, unsigned long long from, unsigned long long to,
             unsigned int flags, void *ctx) {
  file_context *fctx = (file_context *)(ctx);
  fctx->number_of_matches += 1;
  fctx->matches.insert(std::make_pair(from, to));

  if (fctx->option_print_only_filenames) {
    return HS_SCAN_TERMINATED;
  } else {
    return HS_SUCCESS;
  }
}

int print_match_in_red_color(unsigned int id, unsigned long long from,
                             unsigned long long to, unsigned int flags,
                             void *ctx) {
  auto *fctx = (line_context *)(ctx);
  const char *line_data = fctx->data;
  auto &lines = fctx->lines;
  const char *start = *(fctx->current_ptr);
  lines += fmt::format("{}", std::string_view(start, line_data + from - start));
  lines += fmt::format(fg(fmt::color::red), "{}",
                       std::string_view(&line_data[from], to - from));
  *(fctx->current_ptr) = line_data + to;
  return 0;
}

void process_matches(hs_database_t *database, const char *filename,
                     char *buffer, std::size_t bytes_read, file_context &ctx,
                     std::size_t &current_line_number, std::string &lines,
                     bool print_filename, bool is_stdout,
                     bool show_line_numbers) {
  std::string_view chunk(buffer, bytes_read);
  const char *start = buffer;
  const char *ptr = buffer;
  for (const auto &match : ctx.matches) {
    const auto previous_line_number = current_line_number;
    auto line_count = std::count(ptr, start + match.first, '\n');
    current_line_number += line_count;

    if (current_line_number == previous_line_number &&
        previous_line_number > 0) {
      // Ignore match, it's in the same line as the previous match
      continue;
    }

    ptr = start + match.second;

    auto end_of_line = chunk.find_first_of('\n', match.second);
    if (end_of_line == std::string_view::npos) {
      end_of_line = bytes_read;
    }

    auto start_of_line = chunk.find_last_of('\n', match.first);
    if (start_of_line == std::string_view::npos) {
      start_of_line = 0;
    } else {
      start_of_line += 1;
    }

    std::string_view line(start + start_of_line, end_of_line - start_of_line);
    if (is_stdout) {
      if (show_line_numbers) {
        lines += fmt::format(fg(fmt::color::green), "{}:", current_line_number);
      }

      const char *line_ptr = line.data();

      line_context nested_ctx{line_ptr, lines, &line_ptr};
      if (hs_scan(database, &buffer[start_of_line], end_of_line - start_of_line,
                  0, ctx.local_scratch, print_match_in_red_color,
                  &nested_ctx) != HS_SUCCESS) {
        return;
      }

      if (line_ptr != (&buffer[start_of_line] + end_of_line - start_of_line)) {
        // some left over
        lines += std::string(line_ptr, &buffer[start_of_line] + end_of_line -
                                           start_of_line - line_ptr);
      }

      lines += '\n';
    } else {
      if (print_filename) {
        if (show_line_numbers) {
          lines += fmt::format("{}:{}:{}\n", filename, current_line_number,
                               std::string_view(&buffer[start_of_line],
                                                end_of_line - start_of_line));
        } else {
          lines += fmt::format("{}:{}\n", filename,
                               std::string_view(&buffer[start_of_line],
                                                end_of_line - start_of_line));
        }
      } else {
        if (show_line_numbers) {
          lines += fmt::format("{}:{}\n", current_line_number,
                               std::string_view(&buffer[start_of_line],
                                                end_of_line - start_of_line));
        } else {
          lines += fmt::format("{}\n",
                               std::string_view(&buffer[start_of_line],
                                                end_of_line - start_of_line));
        }
      }
    }
  }
}

#include <match_handler.hpp>
#include <unordered_map>

int on_match(unsigned int id, unsigned long long from, unsigned long long to,
             unsigned int flags, void *ctx) {
  file_context *fctx = (file_context *)(ctx);
  fctx->number_of_matches += 1;

  {
    std::lock_guard<std::mutex> lock{fctx->match_mutex};
    fctx->matches.push_back(std::make_pair(from, to));
  }

  if (fctx->option_print_only_filenames) {
    return HS_SCAN_TERMINATED;
  } else {
    return HS_SUCCESS;
  }
}

void process_matches(const char *filename, char *buffer, std::size_t bytes_read,
                     file_context &ctx, std::size_t &current_line_number,
                     std::string &lines, bool print_filename, bool is_stdout,
                     bool show_line_numbers) {
  std::string_view chunk(buffer, bytes_read);

  // Pre process for entries with the same starting position
  // e.g., for the match list {{8192, 8214}, {8192, 8215}}
  // the list can be updated to {{8192, 8215}}
  //
  // Also handle the case where a second match
  // starts somewhere inside the first match
  // from              to
  // ^^^^^^^^^^^^^^^^^^^^
  //        from2            to2
  //        ^^^^^^^^^^^^^^^^^^^^
  std::vector<std::pair<std::size_t, std::size_t>> reduced_matches{};
  std::pair<std::size_t, std::size_t> previous{};
  for (const auto &match : ctx.matches) {
    if (!reduced_matches.empty()) {

      auto previous = reduced_matches.back();

      // previous has a value
      // decide based on previous
      if (match.first == previous.first) {
        // current match has the same start as the previous match
        reduced_matches.pop_back();
        reduced_matches.push_back(match);
      }
      else if (previous.first < match.first && match.first < previous.second) {
        // current match 'from' inside previous match
        if (match.second < previous.second) {
          // current match is entirely inside previous match
          // ignore this mtach
        } else if (match.second >= previous.second) {
          // Keep the match
          reduced_matches.push_back(match);
        }
      }
      else {
        // save this match
        reduced_matches.push_back(match);
      }
    } else {
      reduced_matches.push_back(match);
    }
  }
  
  char *start = buffer;
  auto previous_line_number = current_line_number;
  bool first{true};
  std::size_t previous_start_of_line{0};
  // std::size_t previous_end_of_line{0};
  std::pair<std::size_t, std::size_t> previous_match{};

  for (const auto &match : reduced_matches) {

    auto &[from, to] = match;
    // fmt::print("MATCH {},{}\n", from, to);

    auto start_of_line = chunk.find_last_of('\n', from);
    if (start_of_line == std::string_view::npos) {
      start_of_line = 0;
    } else {
      start_of_line += 1;
    }

    auto end_of_line = chunk.find_first_of('\n', to);
    if (end_of_line == std::string_view::npos) {
      end_of_line = bytes_read;
    } else if (end_of_line < to) {
      end_of_line = to;
    }

    // fmt::print("LINE {},{}\n", start_of_line, end_of_line);

    if (start <= buffer + from) {
      auto line_count = std::count(start, buffer + from, '\n');
      current_line_number = previous_line_number + line_count;

      if (!first && current_line_number == previous_line_number) {
        // Another match in the same line as the previous match
        // 
        // Remove from `lines` and re-add but now color next match as well

        // lines already has the previous match
        if (previous_match.second <= from && to <= end_of_line) {
          // second match is on the same line and starts after 
          // the first match (there's no intersection)

          // remove everything from previous_match.to till end_of_line
          const auto num_characters_to_remove = end_of_line - previous_match.second + 1 /* +1 for the newline character */;
          lines = lines.substr(0, lines.size() - num_characters_to_remove);

          lines += fmt::format("{}", chunk.substr(previous_match.second, from - previous_match.second));
          if (is_stdout) {
            lines += fmt::format(fg(fmt::color::red), chunk.substr(from, to - from));
          } else {
            lines += fmt::format(chunk.substr(from, to - from));
          }
          lines += fmt::format("{}", chunk.substr(to, end_of_line - to));
          lines += "\n";
        } else if (previous_match.first < from && previous_match.second > from && to <= end_of_line) {
          // current match starts before previous match end
          // Remove till current_match.from
          // Re-add current_match.from -> current_match.to (colored RED)
          const auto num_characters_to_remove = end_of_line - from + 1 /* +1 for the newline character */;
          lines = lines.substr(0, lines.size() - num_characters_to_remove);
          if (is_stdout) {
            lines += fmt::format(fg(fmt::color::red), chunk.substr(from, to - from));
          } else {
            lines += fmt::format(chunk.substr(from, to - from));
          }
          lines += fmt::format("{}", chunk.substr(to, end_of_line - to));
          lines += "\n";
        }
        
        previous_start_of_line = start_of_line;
        // previous_end_of_line = end_of_line;
        previous_match = match;
        previous_line_number = current_line_number;
        start = buffer + to;
        continue;
      }

      previous_line_number = current_line_number;
      start = buffer + to;
    }
    else {
      // start > buffer + from
      // 
      // current match starts before previous match end
      // Remove till current_match.from
      // Re-add current_match.from -> current_match.to (colored RED)

      if (start <= buffer + to) {
        const auto num_characters_to_remove = buffer + end_of_line - start + 1 /* +1 for the newline character */;
        lines = lines.substr(0, lines.size() - num_characters_to_remove);
        if (is_stdout) {
          lines += fmt::format(fg(fmt::color::red), chunk.substr(start - buffer, buffer + to - start));
        } else {
          lines += fmt::format(chunk.substr(start - buffer, buffer + to - start));
        }
        lines += fmt::format("{}", chunk.substr(to, end_of_line - to));
        lines += "\n";
        previous_start_of_line = start_of_line;
        // previous_end_of_line = end_of_line;
        previous_match = match;
        previous_line_number = current_line_number;
        start = buffer + to;
        continue;
      }
    }

    if (first || start_of_line > previous_start_of_line) {
      if (show_line_numbers) {
        if (is_stdout) {
          lines +=
              fmt::format(fg(fmt::color::green), "{}:", current_line_number);
        } else {
          if (print_filename) {
            lines += fmt::format("{}:{}:", filename, current_line_number);
          } else {
            lines += fmt::format("{}:", current_line_number);
          }
        }
      } else {
        if (!is_stdout) {
          if (print_filename) {
            lines += fmt::format("{}:", filename);
          }
        }
      }
    }

    auto newlines_in_match = std::count(buffer + from, buffer + to, '\n');
    if (newlines_in_match > 0) {
      current_line_number += newlines_in_match;
      previous_line_number = current_line_number;
    }

    lines += fmt::format("{}", chunk.substr(start_of_line, from - start_of_line));
    if (is_stdout) {
      lines += fmt::format(fg(fmt::color::red), "{}", chunk.substr(from, to - from));
    } else {
      lines += fmt::format("{}", chunk.substr(from, to - from));
    }
    if (end_of_line > to) {
      lines += fmt::format("{}\n", chunk.substr(to, end_of_line - to));
    } else {
      lines += "\n";
    }

    previous_start_of_line = start_of_line;
    // previous_end_of_line = end_of_line;
    previous_match = match;
    if (first) {
      first = false;
    }
  }
}

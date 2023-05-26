#include <map>
#include <match_handler.hpp>
#include <unordered_map>
#include <vector>

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

std::size_t process_matches(
    const char *filename, char *buffer, std::size_t bytes_read,
    std::vector<std::pair<unsigned long long, unsigned long long>> &matches,
    std::size_t &current_line_number, std::string &lines, bool print_filename,
    bool is_stdout, bool show_line_numbers, bool show_column_numbers, bool print_only_matching_parts,
    const std::optional<std::size_t> &max_column_limit) {
  std::string_view chunk(buffer, bytes_read);

  static bool apply_column_limit = max_column_limit.has_value();

  std::map<std::size_t, std::vector<std::pair<std::size_t, std::size_t>>>
      line_number_match;
  {
    char *index = buffer;
    std::size_t previous_line_number = current_line_number;
    for (auto &match : matches) {

      auto &[from, to] = match;
      auto line_count = std::count(index, buffer + from, '\n');
      current_line_number = previous_line_number + line_count;

      if (line_number_match.find(current_line_number) ==
          line_number_match.end()) {
        // line number not in map
        line_number_match.insert(std::make_pair(
            current_line_number,
            std::vector<std::pair<std::size_t, std::size_t>>{match}));
      } else {
        auto &other_matches_in_line = line_number_match.at(current_line_number);
        auto &most_recent_match = other_matches_in_line.back();

        // Cover a few cases here:

        // Case 1: Two matches have the same start
        if (most_recent_match.first == from) {
          if (to > most_recent_match.second) {
            most_recent_match.second = to; // update the to
          }
        }

        // Case 2: The second match is entirely inside the first match
        else if (most_recent_match.first < from &&
                 most_recent_match.first < to &&
                 most_recent_match.second > from &&
                 most_recent_match.second > to) {
          // don't add this match
        }

        // Case 3:
        else if (most_recent_match.first < from &&
                 most_recent_match.first < to &&
                 most_recent_match.second > from &&
                 most_recent_match.second < to) {
          // amend the current match
          most_recent_match.second = to;
        }

        // Case 4: The second match happens well after the first match
        else {
          other_matches_in_line.push_back(match);
        }
      }
      previous_line_number = current_line_number;
      index = buffer + from;
    }
  }

  for (auto &matching_line : line_number_match) {
    auto &current_line_number = matching_line.first;
    auto &matches = matching_line.second;

    bool first{true};
    std::size_t start_of_line{0}, end_of_line{0};
    std::size_t index{0};
    bool line_too_long{false};

    for (auto &[from, to] : matches) {

      if (first) {
        start_of_line = chunk.find_last_of('\n', from);
        if (start_of_line == std::string_view::npos) {
          start_of_line = 0;
        } else {
          start_of_line += 1;
        }

        index = start_of_line;

        end_of_line = chunk.find_first_of('\n', to);
        if (end_of_line == std::string_view::npos) {
          end_of_line = bytes_read;
        } else if (end_of_line < to) {
          end_of_line = to;
        }
      }

      if (apply_column_limit) {
        static std::size_t column_limit =
            apply_column_limit ? max_column_limit.value() : 0;
        const auto line_length = end_of_line - start_of_line;
        if (line_length > column_limit) {
          // with line number: [Omitted long line with 2 matches]
          // without line number: [Omitted long matching line]
          if (show_line_numbers) {
            if (is_stdout) {
              lines += fmt::format(fg(fmt::color::green),
                                   "{}:", current_line_number);
              lines += fmt::format("[Omitted long line with {} matches]\n",
                                   matches.size());
            } else {
              lines += fmt::format("{}:[Omitted long line with {} matches]\n",
                                   current_line_number, matches.size());
            }
          } else {
            lines += fmt::format("[Omitted long line with {} matches]\n",
                                 matches.size());
          }
          line_too_long = true;
          break;
        }
      }

      if (first || print_only_matching_parts) {
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

          if (show_column_numbers) {
            lines += fmt::format("{}:", (from - start_of_line) + 1);
          }

        } else {
          if (!is_stdout) {
            if (print_filename) {
              lines += fmt::format("{}:", filename);
            }
          }
        }
        first = false;
      }

      if (print_only_matching_parts) {
        if (is_stdout) {
          lines += fmt::format(fg(fmt::color::red), "{}\n",
                               chunk.substr(from, to - from));
        } else {
          lines += fmt::format("{}\n", chunk.substr(from, to - from));
        }
      } else {
        lines += fmt::format("{}", chunk.substr(index, from - index));
        if (is_stdout) {
          lines += fmt::format(fg(fmt::color::red), "{}",
                               chunk.substr(from, to - from));
        } else {
          lines += fmt::format("{}", chunk.substr(from, to - from));
        }
        index = to;
      }
    }

    if (!print_only_matching_parts && !line_too_long) {
      if (index <= end_of_line) {
        lines += fmt::format("{}", chunk.substr(index, end_of_line - index));
        lines += "\n";
      }
    }
  }

  // Return the number of matching lines
  return line_number_match.size();
}

// NOTE:
// Only call this function when:
//
//   is_stdout = false
//   print_only_matching_parts = false
//
// This function is optimized for this case
// and assumes that HS_FLAG_SOM_LEFTMOST is not used
// when compiling the HyperScan database
std::size_t process_matches_nocolor_nostdout(
    const char *filename, char *buffer, std::size_t bytes_read,
    std::vector<std::pair<unsigned long long, unsigned long long>> &matches,
    std::size_t &current_line_number, std::string &lines, bool print_filename,
    bool is_stdout, bool show_line_numbers, bool show_column_numbers, bool print_only_matching_parts,
    const std::optional<std::size_t> &max_column_limit) {
  std::string_view chunk(buffer, bytes_read);
  static bool apply_column_limit = max_column_limit.has_value();

  // This map is of the form:
  // {
  //    line_number_1: to_1,
  //    line_number_2: to_2
  // }
  std::map<std::size_t, std::size_t> line_number_match;
  {
    char *index = buffer;
    std::size_t previous_line_number = current_line_number;
    for (auto &match : matches) {

      auto &[_, to] = match;
      if (index > buffer + to) {
        continue;
      }
      auto line_count = std::count(index, buffer + to, '\n');
      current_line_number = previous_line_number + line_count;
      // fmt::print("{},{}\n", current_line_number, to);

      if (line_number_match.find(current_line_number) ==
          line_number_match.end()) {
        // line number not in map
        // save the match
        line_number_match.insert(std::make_pair(current_line_number, to));
      }
      previous_line_number = current_line_number;
      index = buffer + to;
    }
  }

  for (auto &matching_line : line_number_match) {
    auto &current_line_number = matching_line.first;
    auto &to = matching_line.second;

    std::size_t start_of_line{0}, end_of_line{0};

    const auto from = to > 0 ? to - 1 : to;
    start_of_line = chunk.find_last_of('\n', from);
    if (start_of_line == std::string_view::npos) {
      start_of_line = 0;
    } else {
      start_of_line += 1;
    }

    end_of_line = chunk.find_first_of('\n', to);
    if (end_of_line == std::string_view::npos) {
      end_of_line = bytes_read;
    } else if (end_of_line < to) {
      end_of_line = to;
    }

    if (apply_column_limit) {
      static std::size_t column_limit =
          apply_column_limit ? max_column_limit.value() : 0;
      const auto line_length = end_of_line - start_of_line;
      if (line_length > column_limit) {
        // with line number: [Omitted long line with 2 matches]
        // without line number: [Omitted long matching line]
        if (show_line_numbers) {
          lines += fmt::format("{}:[Omitted long line with {} matches]\n",
                               current_line_number, matches.size());
        } else {
          lines += fmt::format("[Omitted long line with {} matches]\n",
                               matches.size());
        }
        continue;
      }
    }

    if (show_line_numbers) {
      if (print_filename) {
        lines += fmt::format("{}:{}:", filename, current_line_number);
      } else {
        lines += fmt::format("{}:", current_line_number);
      }
    } else {
      if (print_filename) {
        lines += fmt::format("{}:", filename);
      }
    }

    lines += fmt::format(
        "{}\n", chunk.substr(start_of_line, end_of_line - start_of_line));
  }

  // Return the number of matching lines
  return line_number_match.size();
}
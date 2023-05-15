#include <match_handler.hpp>
#include <unordered_map>
#include <vector>
#include <map>

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

  std::map<std::size_t, std::vector<std::pair<std::size_t, std::size_t>>> line_number_match;
  {
    char* index = buffer;
    std::size_t previous_line_number = current_line_number;
    for (auto& match: ctx.matches) {

      auto& [from, to] = match;
      auto line_count = std::count(index, buffer + from, '\n');
      current_line_number = previous_line_number + line_count;

      if (line_number_match.find(current_line_number) == line_number_match.end()) {
        // line number not in map
        line_number_match.insert(std::make_pair(current_line_number, std::vector<std::pair<std::size_t, std::size_t>>{match}));
      } else {
        auto& other_matches_in_line = line_number_match.at(current_line_number);
        auto& most_recent_match = other_matches_in_line.back();

        // Cover a few cases here:

        // Case 1: Two matches have the same start 
        if (most_recent_match.first == from) {
          if (to > most_recent_match.second) {
            most_recent_match.second = to; // update the to
          }
        }

        // Case 2: The second match is entirely inside the first match
        else if (most_recent_match.first < from && most_recent_match.first < to && most_recent_match.second > from && most_recent_match.second > to) {
          // don't add this match
        }

        // Case 3: 
        else if (most_recent_match.first < from && most_recent_match.first < to && most_recent_match.second > from && most_recent_match.second < to) {
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

  for (auto& matching_line: line_number_match) {
    auto& current_line_number = matching_line.first;
    auto& matches = matching_line.second;

    bool first{true};
    std::size_t start_of_line{0}, end_of_line{0};
    std::size_t index{0};

    for (auto& [from, to]: matches) {

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

        first = false;
      }

      lines += fmt::format("{}", chunk.substr(index, from - index));
      lines += fmt::format(fg(fmt::color::red), "{}", chunk.substr(from, to - from));
      index = to;
    }

    if (index <= end_of_line) {
      lines += fmt::format("{}", chunk.substr(index, end_of_line - index));
      lines += "\n";
    }
  }
}

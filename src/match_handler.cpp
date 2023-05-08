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
void process_matches(const char *filename,
                     char *buffer, std::size_t bytes_read, file_context &ctx,
                     std::size_t &current_line_number, std::string &lines,
                     bool print_filename, bool is_stdout,
                     bool show_line_numbers) {
  std::string_view chunk(buffer, bytes_read);
  char *start = buffer;
  auto previous_line_number = current_line_number;  

  bool first{true};
  std::size_t previous_start_of_line{0};
  std::size_t previous_end_of_line{0};
  std::size_t index{0};
  
  for (const auto &match : ctx.matches) {

    auto& [from, to] = match;

    auto start_of_line = chunk.find_last_of('\n', from);
    if (start_of_line == std::string_view::npos) {
      start_of_line = 0;
    } else {
      start_of_line += 1;
    }

    if (first) {
      index = start_of_line;
    } else {
      if (start_of_line > previous_end_of_line) {
        // This is a different line
        // Add the remainder of the previous match [start, from, to, end]
        //                                                       ^^^^^^^ this bit
        lines += fmt::format("{}\n", chunk.substr(index, previous_end_of_line - index));
        index = start_of_line;
      } 
    }
    
    auto end_of_line = chunk.find_first_of('\n', to);
    if (end_of_line == std::string_view::npos) {
      end_of_line = bytes_read;
    }
    
    auto line_count = std::count(start, buffer + from, '\n');
    current_line_number = previous_line_number + line_count;
    previous_line_number = current_line_number;
    start = buffer + to;

    // All characters up to the first match

    if (first || start_of_line > previous_start_of_line) {
      if (show_line_numbers) {
        if (is_stdout) {
          lines += fmt::format(fg(fmt::color::green), "{}:", current_line_number);
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
    
    lines += fmt::format("{}", chunk.substr(index, from - index));
    index = from;
    if (is_stdout) {    
      lines += fmt::format(fg(fmt::color::red), "{}", chunk.substr(index, to - from));
    } else {
      lines += fmt::format("{}", chunk.substr(index, to - from));
    }
    index = to;

    previous_start_of_line = start_of_line;
    previous_end_of_line = end_of_line;
    if (first) {
      first = false;
    }
  }
  lines += fmt::format("{}\n", chunk.substr(index, previous_end_of_line - index));
}

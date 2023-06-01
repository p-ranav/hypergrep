#include <fstream>
#include <hypergrep/compiler.hpp>
#include <hypergrep/search_options.hpp>

void read_pattern_file(const std::string &filename,
                       std::vector<std::string> &pattern_list) {
  std::ifstream file(filename);

  if (file.is_open()) {
    std::string line;
    while (std::getline(file, line)) {
      pattern_list.push_back(line);
    }
  } else {
    const auto error = fmt::format("Error: Unable to open file {}", filename);
    throw std::runtime_error(error.c_str());
  }
}

void initialize_search(std::string &pattern, argparse::ArgumentParser &program,
                       search_options &options, hs_database **database,
                       hs_scratch **scratch, hs_database **file_filter_database,
                       hs_scratch **file_filter_scratch) {

  options.search_binary_files = program.get<bool>("--text");
  options.count_matching_lines = program.get<bool>("-c");
  options.count_matches = program.get<bool>("--count-matches");
  options.compile_pattern_as_literal = program.get<bool>("-F");
  options.num_threads = program.get<unsigned>("-j");
  auto show_line_number = program.get<bool>("-n");
  auto hide_line_number = program.get<bool>("-N");
  options.exclude_submodules = program.get<bool>("--ignore-submodules");
  options.ignore_case = program.get<bool>("-i");
  options.count_include_zeros = program.get<bool>("--include-zero");
  options.print_filenames = !(program.get<bool>("-I"));
  options.print_only_filenames = program.get<bool>("-l");
  if (program.is_used("--filter")) {
    options.filter_file_pattern = program.get<std::string>("--filter");
    options.filter_files = true;
    if (!construct_file_filtering_hs_database(file_filter_database,
                                              file_filter_scratch, options)) {
      throw std::runtime_error("Error compiling pattern " +
                               options.filter_file_pattern);
    }
  }

  if (program.is_used("-M")) {
    options.max_column_limit = program.get<std::size_t>("-M");
  }

  if (program.is_used("--max-filesize")) {
    const auto max_file_size_spec = program.get<std::string>("--max-filesize");
    options.max_file_size = size_to_bytes(max_file_size_spec);
  }

  options.print_only_matching_parts = program.get<bool>("-o");

  // Check if word boundary is requested
  if (program.get<bool>("-w")) {
    pattern = "\\b" + pattern + "\\b";

    // This cannot work as a literal anymore
    options.compile_pattern_as_literal = false;
  }

  options.ltrim_each_output_line = program.get<bool>("--trim");
  options.use_ucp = program.get<bool>("--ucp");
  options.search_hidden_files = program.get<bool>("--hidden");

  options.is_stdout = isatty(STDOUT_FILENO) == 1;

  if (options.is_stdout) {
    // By default show line numbers
    // unless -N is used
    options.show_line_numbers = (!hide_line_number);
  } else {
    // By default hide line numbers
    // unless -n is used
    options.show_line_numbers = show_line_number;
  }

  options.show_column_numbers = program.get<bool>("--column");
  if (options.show_column_numbers) {
    options.show_line_numbers = true;
  }

  options.show_byte_offset = program.get<bool>("-b");

  options.perform_search = !program.get<bool>("--files");
  if (options.perform_search) {

    auto pattern_list = program.get<std::vector<std::string>>("-e");

    if (program.is_used("-f")) {
      // read from pattern file and append
      // to the pattern list
      const auto pattern_files = program.get<std::vector<std::string>>("-f");

      for (const auto &pattern_file : pattern_files) {
        read_pattern_file(pattern_file, pattern_list);
      }
    }

    if (program.get<bool>("-w")) {
      // Add word boundary around each pattern
      for (auto &pattern : pattern_list) {
        pattern = "\\b" + pattern + "\\b";
      }

      // This cannot work as a literal anymore
      options.compile_pattern_as_literal = false;
    }

    if (pattern_list.empty()) {
      compile_hs_database(database, scratch, options, {pattern});
    } else {
      compile_hs_database(database, scratch, options, pattern_list);
    }
  }
}
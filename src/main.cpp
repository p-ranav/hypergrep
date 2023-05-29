#include <constants.hpp>
#include <directory_search.hpp>
#include <file_search.hpp>
#include <git_index_search.hpp>
#include <print_help.hpp>

void perform_search(std::string& pattern, std::string_view path, argparse::ArgumentParser& program) {
  if (!isatty(fileno(stdin))) {
    // Program was called from a pipe

    file_search s(pattern, program);
    std::string line;
    std::size_t current_line_number{1};
    while (std::getline(std::cin, line)) {
      // Process line here
      bool break_loop{false};
      s.scan_line(line, current_line_number, break_loop);
      if (break_loop) {
        break;
      }
    }
  } else {
    if (std::filesystem::is_regular_file(path)) {
      file_search s(pattern, program);
      s.run(path);
    } else {
      const auto current_path = std::filesystem::current_path();

      if (std::filesystem::exists(std::filesystem::path(path) / ".git")) {
        if (chdir(path.data()) == 0) {
          git_index_search s(pattern, current_path, program);
          s.run(".");
          if (chdir(current_path.c_str()) != 0) {
            throw std::runtime_error("Failed to restore path");
          }
        }
      } else {
        directory_search s(pattern, current_path, program);
        s.run(path);
      }
    }
  }
}

int main(int argc, char **argv) {

  argparse::ArgumentParser program("hg", VERSION.data(), argparse::default_arguments::none);

  program.add_argument("-h", "--help")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-v", "--version")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-b", "--byte-offset")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--column")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-c", "--count")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--count-matches")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--exclude-submodules")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--files")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--filter");

  program.add_argument("-F", "--fixed-strings")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--hidden")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-i", "--ignore-case")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--include-zero")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-I", "--no-filename")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-l", "--files-with-matches")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-M", "--max-columns")
      .scan<'d', std::size_t>();

  program.add_argument("--max-filesize");

  program.add_argument("-n", "--line-number")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-N", "--no-line-number")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-o", "--only-matching")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-a", "--text")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--ucp")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-w", "--word-regexp")
      .default_value(false)
      .implicit_value(true);

  const auto max_concurrency = std::thread::hardware_concurrency();
  const auto default_num_threads =
      max_concurrency > 1 ? max_concurrency - 1 : 1;
  program.add_argument("-j", "--threads")
      .help("The number of threads to use")
      .default_value(default_num_threads)
      .scan<'d', unsigned>();

  program.add_argument("patterns_and_paths")
    .default_value(std::vector<std::string>{})
    .remaining();

  try {
    program.parse_args(argc, argv);
  } catch (const std::runtime_error &err) {
    std::cerr << err.what() << std::endl;
    std::cerr << "\nFor more information try --help\n";
    return 1;
  }

  if (program.is_used("-h")) {
    print_help();
    return 0;
  }
  else if (program.is_used("-v")) {
    fmt::print("{}\n", VERSION);
    return 0;
  }

  // If --files is not used
  // pattern is required
  const auto files_used = program.get<bool>("--files");

  if (files_used) {
    // Treat everything in patterns_and_paths
    // as a list of paths
    //
    // If empty, just search "."
    auto empty_pattern = std::string{};
    auto paths = program.get<std::vector<std::string>>("patterns_and_paths");
    if (paths.empty()) {
      perform_search(empty_pattern, ".", program);
    } else {
      for (const auto& path: paths) {
        perform_search(empty_pattern, path, program);
      }
    }
  }
  else {
    // Treat first in patterns_and_paths
    // as the pattern
    //
    // The rest are paths to process
    //
    // If size == 1 (i.e. just a pattern provided), just search "."
    auto patterns_and_paths = program.get<std::vector<std::string>>("patterns_and_paths");
    const auto size = patterns_and_paths.size();

    if (size == 0) {
      // TODO: print meaningful error message + USAGE
      // e.g., "expected <PATTERN>"

      // TODO: return here
    }
    
    auto& pattern = patterns_and_paths[0];

    if (size == 1) {
      // Path not provided
      // Default to current directory
      perform_search(pattern, ".", program);
    }
    else {
      for (std::size_t i = 1; i < patterns_and_paths.size(); ++i) {
        perform_search(pattern, patterns_and_paths[i], program);
      }
    }
  }
  return 0;
}

#include <constants.hpp>
#include <directory_search.hpp>
#include <file_search.hpp>
#include <git_index_search.hpp>
#include <print_help.hpp>

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

  program.add_argument("pattern")
      .default_value(std::string{"."})
      .help("regular expression pattern");

  program.add_argument("path")
      .default_value(std::string{"."})
      .help("path to search");

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
  if (!files_used && !program.is_used("pattern")) {
    std::cerr << "1 argument(s) expected. 0 provided." << std::endl;
    std::cerr << "\nFor more information try --help\n";
    return 1;
  }

  auto path = program.get<std::string>("path");

  if (!isatty(fileno(stdin))) {
    // Program was called from a pipe

    file_search s(program);
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
      file_search s(program);
      s.run(path);
    } else {

      const auto current_path = std::filesystem::current_path();

      if (std::filesystem::exists(std::filesystem::path(path) / ".git")) {
        if (chdir(path.c_str()) == 0) {
          git_index_search s(current_path, program);
          s.run(".");
          if (chdir(current_path.c_str()) != 0) {
            throw std::runtime_error("Failed to restore path");
          }
        }
      } else {
        directory_search s(current_path, program);
        s.run(path);
      }
    }
  }

  return 0;
}

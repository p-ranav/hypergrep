#include <directory_search.hpp>
#include <file_search.hpp>

int main(int argc, char **argv) {

  argparse::ArgumentParser program("hg");

  program.add_argument("-c", "--count")
      .help("This flag suppresses normal output and shows the number of lines "
            "that match the given patterns for each file searched.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--exclude-submodules")
      .help("Exclude submodules from the search")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-f", "--filter")
      .help("Filter files based on a pattern");

  program.add_argument("--hidden")
      .help("Search hidden files and directories. By default, hidden files and directories are skipped.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-i", "--ignore-case")
      .help("When this flag is provided, the given patterns will be searched "
            "case insensitively.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-l", "--files-with-matches")
      .help("print only filenames")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--max-file-size")
      .help("Ignore files above a certain size");

  program.add_argument("-n", "--line-number")
      .help("Show line numbers (1-based). This is enabled by default when "
            "searching in a terminal.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-N", "--no-line-number")
      .help("Suppress line numbers. This is enabled by default when not "
            "searching in a terminal.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--ucp")
      .help("Use unicode properties, rather than the default ASCII interpretations, for character mnemonics like \\w and \\s as well as the POSIX character classes.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-w", "--word-regexp")
      .help(
          "Only show matches surrounded by word boundaries. This is equivalent "
          "to putting \b before and after all of the search patterns.")
      .default_value(false)
      .implicit_value(true);

  const auto max_concurrency = std::thread::hardware_concurrency();
  const auto default_num_threads =
      max_concurrency > 1 ? max_concurrency - 1 : 1;
  program.add_argument("-j", "--threads")
      .help("The approximate number of threads to use")
      .default_value(default_num_threads)
      .scan<'d', unsigned>();

  program.add_argument("pattern").required().help("regular expression pattern");

  program.add_argument("path")
      .default_value(std::string{"."})
      .help("path to search");

  try {
    program.parse_args(argc, argv);
  } catch (const std::runtime_error &err) {
    std::cerr << err.what() << std::endl;
    std::cerr << program;
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
      s.scan_line(line, current_line_number);
    }
  }
  else {
    if (std::filesystem::is_regular_file(path)) {
      file_search s(program);
      s.run(path);
    } else {
      directory_search s(program);
      s.run(path);
    }
  }

  return 0;
}

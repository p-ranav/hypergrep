#include <directory_search.hpp>
#include <file_search.hpp>
#include <git_index_search.hpp>

int main(int argc, char **argv) {

  argparse::ArgumentParser program("hg");

  program.add_argument("-c", "--count")
      .help("This flag suppresses normal output and shows the number of lines "
            "that match the given patterns for each file searched.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--count-matches")
      .help("This flag suppresses normal output and shows the number of "
            "individual "
            "matches of the given patterns for each file searched.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--exclude-submodules")
      .help("Exclude submodules from the search")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--files")
      .help("Print each file that would be searched without actually "
            "performing the search")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-f", "--filter")
      .help(
          "Filter files based on a pattern, e.g., --filter '\\.(c|cpp|h|hpp)'");

  program.add_argument("--hidden")
      .help("Search hidden files and directories. By default, hidden files and "
            "directories are skipped.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-i", "--ignore-case")
      .help("When this flag is provided, the given patterns will be searched "
            "case insensitively.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-I", "--no-filename")
      .help("Never print the file path with the matched lines. This is the "
            "default when searching one file or stdin.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-l", "--files-with-matches")
      .help("print only filenames")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-M", "--max-columns")
      .help("Don't print lines longer than this limit")
      .scan<'d', std::size_t>();

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

  program.add_argument("-o", "--only-matching")
      .help("Print only matched parts of a line.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-a", "--text")
      .help("Search binary files as if they were text. When this flag is "
            "present, binary file detection is disabled. This "
            "means that when a binary file is searched, its contents may be "
            "printed if there is a match. This may cause escape codes to be "
            "printed that alter the behavior of your terminal.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--ucp")
      .help("Use unicode properties, rather than the default ASCII "
            "interpretations, for character mnemonics like \\w and \\s as well "
            "as the POSIX character classes.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-w", "--word-regexp")
      .help(
          "Only show matches surrounded by word boundaries. This is equivalent "
          "to putting \\b before and after all of the search patterns.")
      .default_value(false)
      .implicit_value(true);

  const auto max_concurrency = std::thread::hardware_concurrency();
  const auto default_num_threads =
      max_concurrency > 1 ? max_concurrency - 1 : 1;
  program.add_argument("-j", "--threads")
      .help("The approximate number of threads to use")
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
    std::cerr << program;
    return 1;
  }

  // If --files is not used
  // pattern is required
  const auto files_used = program.get<bool>("--files");
  if (!files_used && !program.is_used("pattern")) {
    std::cerr << "1 argument(s) expected. 0 provided." << std::endl;
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

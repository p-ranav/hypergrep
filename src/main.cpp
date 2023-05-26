#include <constants.hpp>
#include <directory_search.hpp>
#include <file_search.hpp>
#include <git_index_search.hpp>

int main(int argc, char **argv) {

  argparse::ArgumentParser program("hg", VERSION.data());

  program.add_argument("-b", "--byte-offset")
      .help("Print the 0-based byte offset within the input\n\t\t\t\tfile before each line of output. \n\t\t\t\tIf -o (--only-matching) is used, print the\n\t\t\t\toffset of the matching part itself.\n")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--column")
      .help("Show column numbers (1-based). This only shows\n\t\t\t\tthe column numbers "
            "for the first match on each\n\t\t\t\tline.\n")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-c", "--count")
      .help("This flag suppresses normal output and shows\n\t\t\t\tthe number of lines "
            "that match the given\n\t\t\t\tpatterns for each file searched.\n")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--count-matches")
      .help("This flag suppresses normal output and shows the\n\t\t\t\tnumber of "
            "individual "
            "matches of the given \n\t\t\t\tpatterns for each file searched.\n")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--exclude-submodules")
      .help("Exclude git submodules from the search\n")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--files")
      .help("Print each file that would be searched without\n\t\t\t\tactually "
            "performing the search\n")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-f", "--filter")
      .help(
          "Filter files based on a regex pattern,e.g.,\n\t\t\t\t--filter '/(include|src)/.*\\.(c|cpp|h|hpp)'\n");

  program.add_argument("-F", "--fixed-strings")
      .help("Treat the pattern as a literal string instead of\n\t\t\t\ta regex. "
            "Special regex meta characters such as\n\t\t\t\t"
            ".(){}*+ do not need to be escaped.\n")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--hidden")
      .help("Search hidden files and directories. By default,\n\t\t\t\thidden files and "
            "directories are skipped.\n")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-i", "--ignore-case")
      .help("When this flag is provided, the given patterns\n\t\t\t\twill be searched "
            "case insensitively.\n")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-I", "--no-filename")
      .help("Never print the file path with the\n\t\t\t\tmatched lines. This is the "
            "default when\n\t\t\t\tsearching one file or stdin.\n")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-l", "--files-with-matches")
      .help("Print the paths with at least one match\n\t\t\t\tand suppress match contents.\n")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-M", "--max-columns")
      .help("Don't print lines longer than this limit in\n\t\t\t\tbytes. Longer lines are omitted, and only the\n\t\t\t\tnumber of matches in that line is printed.\n")
      .scan<'d', std::size_t>();

  program.add_argument("--max-filesize")
      .help("Ignore files above a certain size. The input\n\t\t\t\taccepts suffixes of K, M or G. If no suffix\n\t\t\t\tis provided the input is treated as bytes.\n\t\t\t\te.g., --max-filesize 50K or --max-filesize 80M\n");

  program.add_argument("-n", "--line-number")
      .help("Show line numbers (1-based). This is enabled by\n\t\t\t\tdefault when "
            "searching in a terminal.\n")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-N", "--no-line-number")
      .help("Suppress line numbers. This is enabled by\n\t\t\t\tdefault when not "
            "searching in a terminal.\n")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-o", "--only-matching")
      .help("Print only matched parts of a matching line,\n\t\t\t\twith each such part on a separate output line.\n")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-a", "--text")
      .help("Search binary files as if they were text. When\n\t\t\t\tthis flag is "
            "present, binary file detection is\n\t\t\t\tdisabled. "
            "When a binary file is searched, its\n\t\t\t\tcontents may be "
            "printed if there is a match.\n\t\t\t\tThis may cause escape codes to be "
            "printed that\n\t\t\t\talter the behavior of the terminal.\n")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("--ucp")
      .help("Use unicode properties, rather than the default\n\t\t\t\tASCII "
            "interpretations, for character\n\t\t\t\tmnemonics like \\w and \\s as well "
            "as the POSIX\n\t\t\t\tcharacter classes.\n")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-w", "--word-regexp")
      .help(
          "Only show matches surrounded by word boundaries.\n\t\t\t\tThis is equivalent "
          "to putting \\b before\n\t\t\t\tand after all of the search patterns.\n")
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

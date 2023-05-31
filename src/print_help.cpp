#include <constants.hpp>
#include <print_help.hpp>
#include <unistd.h>

void print_heading(bool is_stdout, std::string_view name) {
  if (is_stdout) {
    fmt::print(fmt::emphasis::bold, "{}\n", name);
  } else {
    fmt::print("{}\n", name);
  }
}

void print_option_name(bool is_stdout, std::string_view name,
                       std::string_view arg_placeholder = "") {
  if (is_stdout) {
    fmt::print(fmt::emphasis::bold, "    {}", name);
  } else {
    fmt::print("    {}", name);
  }
  if (arg_placeholder.empty()) {
    fmt::print("\n");
  } else {
    fmt::print(" {}\n", arg_placeholder);
  }
}

void print_description_line(std::string_view line) {
  fmt::print("        {}\n", line);
}

void print_synopsis_command(bool is_stdout) {
  if (is_stdout) {
    fmt::print(fmt::emphasis::bold, "    {}", NAME);
  } else {
    fmt::print("    {}", NAME);
  }
  fmt::print(" [");
  if (is_stdout) {
    fmt::print(fmt::emphasis::bold, "OPTIONS");
  } else {
    fmt::print("OPTIONS");
  }
  fmt::print("] ");
}

void print_synopsis_1(bool is_stdout) {
  print_synopsis_command(is_stdout);

  if (is_stdout) {
    fmt::print(fmt::emphasis::bold, "PATTERN [PATH ...]");
  } else {
    fmt::print("PATTERN [PATH ...]");
  }
  fmt::print("\n");
}

void print_synopsis_2(bool is_stdout) {
  print_synopsis_command(is_stdout);

  if (is_stdout) {
    fmt::print(fmt::emphasis::bold, "--files [PATH ...]");
  } else {
    fmt::print("--files [PATH ...]");
  }
  fmt::print("\n");
}

void print_synopsis_3(bool is_stdout) {
  print_synopsis_command(is_stdout);

  if (is_stdout) {
    fmt::print(fmt::emphasis::bold, "--help");
  } else {
    fmt::print("--help");
  }
  fmt::print("\n");
}

void print_synopsis_4(bool is_stdout) {
  print_synopsis_command(is_stdout);

  if (is_stdout) {
    fmt::print(fmt::emphasis::bold, "--version");
  } else {
    fmt::print("--version");
  }
  fmt::print("\n");
}

void print_help() {
  const auto is_stdout = isatty(STDOUT_FILENO) == 1;

  // Name and Brief Description
  print_heading(is_stdout, "NAME");
  if (is_stdout) {
    fmt::print(fmt::emphasis::bold, "    {}", NAME);
  } else {
    fmt::print("    {}", NAME);
  }
  fmt::print(" - {}\n\n", DESCRIPTION);

  // Synopsis
  print_heading(is_stdout, "SYNOPSIS");
  print_synopsis_1(is_stdout);
  print_synopsis_2(is_stdout);
  print_synopsis_3(is_stdout);
  print_synopsis_4(is_stdout);
  fmt::print("\n");

  // Description
  print_heading(is_stdout, "DESCRIPTION");
  fmt::print(
      "    This section provides a detailed description of your program.\n\n");

  // Pattern
  print_heading(is_stdout, "PATTERN");
  print_option_name(false, "A regular expression used for searching.\n");

  // Path
  print_heading(is_stdout, "PATH");
  print_option_name(
      false,
      "A file or directory to search. Directories are searched recursively.");
  print_option_name(
      false,
      "If a git repository is detected, its git index is loaded and the");
  print_option_name(false, "index entries are iterated using libgit2 and "
                           "searched. If a path is not");
  print_option_name(
      false,
      "provided, the current working directory is recursively searched.\n");

  // Options
  print_heading(is_stdout, "OPTIONS");

  // Byte Offset
  print_option_name(is_stdout, "-b, --byte-offset");
  print_description_line(
      "Print the 0-based byte offset within the input file before each");
  print_description_line(
      "line of output.If -o (--only-matching) is used, print the offset");
  print_description_line("of the matching part itself.\n");

  // Column
  print_option_name(is_stdout, "--column");
  print_description_line(
      "Show column numbers (1-based). This only shows the column");
  print_description_line("numbers for the first match on each line.\n");

  // Count
  print_option_name(is_stdout, "-c, --count");
  print_description_line(
      "This flag suppresses normal output and shows the number of");
  print_description_line(
      "lines that match the given pattern for each file searched\n");

  // Count Matches
  print_option_name(is_stdout, "--count-matches");
  print_description_line(
      "This flag suppresses normal output and shows the number of");
  print_description_line(
      "individual matches of the given pattern for each file searched\n");

  // Exclude git submodules
  print_option_name(is_stdout, "--exclude-submodules");
  print_description_line(
      "For any detected git repository, this option will cause");
  print_description_line("hypergrep to exclude any submodules found.\n");

  // Files
  print_option_name(is_stdout, "--files");
  print_description_line(
      "Print each file that would be searched without actually");
  print_description_line("performing the search\n");

  // Filter
  print_option_name(is_stdout, "--filter", "<FILTER_PATTERN>");
  print_description_line("Filter paths based on a regex pattern, e.g.,\n");
  print_option_name(is_stdout,
                    "        hg --filter '(include|src)/.*\\.(c|cpp|h|hpp)'\n");
  print_description_line(
      "will search C/C++ files in the any */include/* and */src/* paths.\n");

  // Fixed Strings
  print_option_name(is_stdout, "-F, --fixed-strings");
  print_description_line(
      "Treat the pattern as a literal string instead of a regex.");
  print_description_line(
      "Special regex meta characters such as .(){}*+ do not need");
  print_description_line("to be escaped.\n");

  // Help
  print_option_name(is_stdout, "-h, --help");
  print_description_line("Display this help message.\n");

  // Hidden
  print_option_name(is_stdout, "--hidden");
  print_description_line(
      "Search hidden files and directories. By default, hidden files");
  print_description_line(
      "and directories are skipped. A file or directory is considered");
  print_description_line(
      "hidden if its base name starts with a dot character ('.').\n");

  // Ignore case
  print_option_name(is_stdout, "-i, --ignore-case");
  print_description_line(
      "When this flag is provided, the given patterns will be searched");
  print_description_line(
      "case insensitively. The <PATTERN> may still use PCRE tokens");
  print_description_line(
      "(notably (?i) and (?-i)) to toggle case-insensitive matching.\n");

  // Include zero matches
  print_option_name(is_stdout, "--include-zero");
  print_description_line(
      "When used with --count or --count-matches, print the number of");
  print_description_line(
      "matches for each file even if there were zero matches. This is");
  print_description_line("distabled by default.\n");

  // No filename
  print_option_name(is_stdout, "-I, --no-filename");
  print_description_line(
      "Never print the file path with the matched lines. This is the");
  print_description_line("default when searching one file or stdin.\n");

  // Files with matches
  print_option_name(is_stdout, "-l, --files-with-matches");
  print_description_line(
      "Print the paths with at least one match and suppress match contents.\n");

  // Max columns
  print_option_name(is_stdout, "-M, --max-columns", "<NUM>");
  print_description_line(
      "Don't print lines longer than this limit in bytes. Longer lines are");
  print_description_line(
      "omitted, and only the number of matches in that line is printed.\n");

  // Max filesize
  print_option_name(is_stdout, "--max-filesize", "<NUM+SUFFIX?>");
  print_description_line(
      "Ignore files above a certain size. The input accepts suffixes of");
  print_description_line(
      "form K, M or G. If no suffix is provided the input is treated as bytes");
  print_description_line("e.g.,\n");
  print_option_name(is_stdout, "        hg --max-filesize 50K\n");
  print_description_line("will search any files under 50KB in size.\n");

  // Line Number
  print_option_name(is_stdout, "-n, --line-number");
  print_description_line("Show line numbers (1-based). This is enabled by "
                         "defauled when searching");
  print_description_line("in a terminal.\n");

  // No Line Number
  print_option_name(is_stdout, "-N, --no-line-number");
  print_description_line("Suppress line numbers. This is enabled by default "
                         "when not searching in");
  print_description_line("a terminal.\n");

  // Only matching parts
  print_option_name(is_stdout, "-o, --only-matching");
  print_description_line(
      "Print only matched parts of a matching line, with each such part on a");
  print_description_line("separate output line.\n");

  // Binary as text
  print_option_name(is_stdout, "-a, --text");
  print_description_line(
      "Search binary files as if they were text. When this flag is present,");
  print_description_line(
      "binary file detection is disabled. When a binray file is searched, its");
  print_description_line(
      "contents may be printed if there is a match. This may cause escape");
  print_description_line(
      "codes to be printed that alter the behavior of the terminal.\n");

  // UCP
  print_option_name(is_stdout, "--ucp");
  print_description_line(
      "Use unicode properties, rather than the default ASCII interpretations,");
  print_description_line(
      "for character mnemonics like \\w and \\s as well as the POSIX");
  print_description_line("character classes.\n");

  // Version
  print_option_name(is_stdout, "-v, --version");
  print_description_line("Display the version information.\n");

  // Word
  print_option_name(is_stdout, "-w, --word-regexp");
  print_description_line(
      "Only show matches surrounded by word boundaries. This is equivalent to");
  print_description_line(
      "putting \\b before and after the the search pattern.\n");
}
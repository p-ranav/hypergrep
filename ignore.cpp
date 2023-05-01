#include <hs/hs.h>
#include <string>
#include <vector>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <dirent.h>
#include <unistd.h>

// Converts a glob expression to a HyperScan pattern
std::string convert_to_hyper_scan_pattern(const std::string& glob) {

    if (glob.empty()) {
      return "";
    }

    bool dir_pattern{false};
    if (glob[0] == '/') {
      dir_pattern = true;
    }

    std::string pattern = dir_pattern ? "" : "^";

    for (auto it = glob.begin(); it != glob.end(); ++it) {
        switch (*it) {
            case '*':
                pattern += ".*";
                break;
            case '?':
                pattern += ".";
                break;
            case '.':
                pattern += "\\.";
                break;
            case '[':
                {
                    ++it;
                    pattern += "[";
                    while (*it != ']') {
                        if (*it == '\\') {
                            pattern += "\\\\";
                        } else if (*it == '-') {
                            pattern += "\\-";
                        } else {
                            pattern += *it;
                        }
                        ++it;
                    }
                    pattern += "]";
                }
                break;
            case '\\':
                {
                    ++it;
                    if (it == glob.end()) {
                        pattern += "\\\\";
                        break;
                    }
                    switch (*it) {
                        case '*':
                        case '?':
                        case '.':
                        case '[':
                        case '\\':
                            pattern += "\\";
                        default:
                            pattern += *it;
                            break;
                    }
                }
                break;
            default:
                pattern += *it;
                break;
        }
    }
    pattern += dir_pattern ? "" : "$";

    return pattern;
}

std::string parse_gitignore_file(const std::filesystem::path& gitignore_file_path) {
    std::ifstream gitignore_file(gitignore_file_path, std::ios::in | std::ios::binary);
    char buffer[1024];

    if (!gitignore_file.is_open()) {
        std::cerr << "Error opening .gitignore file at " << gitignore_file_path << std::endl;
        return {};
    }

    std::string result{};
    std::string line{};

    while (std::getline(gitignore_file, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '!') {
          continue;
        }

        if (!result.empty())
        {
          result += "|";
        }
        result += "(" + convert_to_hyper_scan_pattern(line) + ")";
    }

    return result;
}

struct ScanContext {
    bool matched;
};

hs_error_t on_match(unsigned int id, unsigned long long from, 
    unsigned long long to, unsigned int flags, void* context)
{
    ScanContext* scanCtx = static_cast<ScanContext*>(context);
    scanCtx->matched = true;
    return HS_SUCCESS;
}

bool fnmatch_hyperscan(std::string patternExpr, const char* str, hs_database_t* database, hs_scratch_t* scratch)
{
  std::string_view path = str;
  if (path.size() > 2 && path[0] == '.' && path[1] == '/')
  {
    path = path.substr(1);
  }

  ScanContext ctx { false };
  // Scan the input string for matches
  if (hs_scan(database, path.data(), path.size(), 0, scratch, &on_match, &ctx) != HS_SUCCESS) {
      std::cerr << "Error scanning input string" << std::endl;
      hs_free_database(database);
      return false;
  }

  // Return true if a match was found
  return ctx.matched;
}

void visit(const std::filesystem::path& path, const std::string& pattern, bool recompile, hs_database_t* local_database, hs_scratch* local_scratch)
{
  for (const auto& entry : std::filesystem::directory_iterator(path, std::filesystem::directory_options::skip_permission_denied))
  {
    const auto& path = entry.path();
    const auto pathstring = path.c_str();
    const auto filename = path.filename().c_str();

    if (filename[0] == '.')
    {
      continue;
    }

    if (entry.is_directory())
    {
      if (!fnmatch_hyperscan(pattern, pathstring, local_database, local_scratch))
      {
        visit(entry.path(), pattern, false, local_database, local_scratch);
      }
    }
    else
    {
      if (!fnmatch_hyperscan(pattern, pathstring, local_database, local_scratch))
      {
        std::cout << pathstring << "\n";
      }
    }
  }
}

int main()
{
  hs_database_t* database = nullptr;
  hs_scratch* scratch = nullptr;
  hs_compile_error_t* compileErr = nullptr;

  auto pattern = parse_gitignore_file(".gitignore");

  // Compile the pattern expression into a Hyperscan database
  if (hs_compile(pattern.c_str(), HS_FLAG_ALLOWEMPTY | HS_FLAG_UTF8, HS_MODE_BLOCK, nullptr, &database, &compileErr) != HS_SUCCESS) {
      std::cerr << "Error compiling pattern expression: " << compileErr->message << std::endl;
      hs_free_compile_error(compileErr);
      return false;
  }

  hs_error_t database_error = hs_alloc_scratch(database, &scratch);
  if (database_error != HS_SUCCESS)
  {
      fprintf(stderr, "Error allocating scratch space\n");
      hs_free_database(database);
      return 1;
  }

  // {
  //   std::cout << pattern << "\n";
  //   std::vector<const char*> inputs = {
  //     "./include/config/FONT_AUTOSELECT",
  //     "./foo/bar/baz.a",
  //     "./foo/bar/baz.o.cmd",
  //     "./tools",
  //     "./src"
  //   };
  //   for (const auto& i : inputs)
  //   {
  //     std::cout << i << " " << fnmatch_hyperscan(pattern, i, database, scratch) << "\n";
  //   }
  // }

  std::cout << pattern << "\n";
  visit(".", pattern, false, database, scratch);

  hs_free_database(database);
}
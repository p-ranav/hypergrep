#include <git_index_search.hpp>
#include <is_binary.hpp>

git_index_search::git_index_search(argparse::ArgumentParser &program) {
  options.count_matching_lines = program.get<bool>("-c");
  options.num_threads = program.get<unsigned>("-j");
  auto show_line_number = program.get<bool>("-n");
  auto hide_line_number = program.get<bool>("-N");
  options.exclude_submodules = program.get<bool>("--exclude-submodules");
  options.ignore_case = program.get<bool>("-i");
  options.print_only_filenames = program.get<bool>("-l");
  if (program.is_used("-f")) {
    options.filter_file_pattern = program.get<std::string>("-f");
    options.filter_files = true;
    if (!construct_file_filtering_hs_database()) {
      throw std::runtime_error("Error compiling pattern " +
                               options.filter_file_pattern);
    }
  }

  if (program.is_used("--max-file-size")) {
    const auto max_file_size_spec = program.get<std::string>("--max-file-size");
    options.max_file_size = size_to_bytes(max_file_size_spec);
  }

  auto pattern = program.get<std::string>("pattern");

  // Check if word boundary is requested
  if (program.get<bool>("-w")) {
    pattern = "\\b" + pattern + "\\b";
  }

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

  compile_hs_database(pattern);
}

git_index_search::~git_index_search() {
  if (file_filter_scratch) {
    hs_free_scratch(file_filter_scratch);
  }
  if (file_filter_database) {
    hs_free_database(file_filter_database);
  }
  for (auto &iter : garbage_collect_index_iterator) {
    if (iter)
      git_index_iterator_free(iter);
  }
  for (auto &idx : garbage_collect_index) {
    if (idx)
      git_index_free(idx);
  }
  for (auto &repo : garbage_collect_repo) {
    if (repo)
      git_repository_free(repo);
  }
}

void git_index_search::run(std::filesystem::path path) {
  git_libgit2_init();
  std::vector<std::thread> consumer_threads(options.num_threads);

  for (std::size_t i = 0; i < options.num_threads; ++i) {

    consumer_threads[i] = std::thread([this, i = i]() {
      char buffer[FILE_CHUNK_SIZE];
      std::string lines{};

      hs_scratch_t *local_scratch = NULL;
      hs_error_t database_error = hs_alloc_scratch(database, &local_scratch);
      if (database_error != HS_SUCCESS) {
        throw std::runtime_error("Error allocating scratch space\n");
      }

      while (true) {
        if (num_files_enqueued > 0) {
          try_dequeue_and_process_path(local_scratch, buffer, lines);
          if (!running && num_files_dequeued == num_files_enqueued) {
            break;
          }
        }
      }

      hs_free_scratch(local_scratch);
    });
  }

  // Try to visit as a git repo
  // If it fails, visit normally
  visit_git_repo(path);

  running = false;

  for (std::size_t i = 0; i < options.num_threads; ++i) {
    consumer_threads[i].join();
  }
}

void git_index_search::compile_hs_database(std::string &pattern) {
  hs_compile_error_t *compile_error = NULL;
  auto error_code = hs_compile(pattern.data(),
                               (options.ignore_case ? HS_FLAG_CASELESS : 0) |
			       HS_FLAG_UTF8 |
			       (options.use_ucp ? HS_FLAG_UCP : 0) |
			       HS_FLAG_SOM_LEFTMOST,
                               HS_MODE_BLOCK, NULL, &database, &compile_error);
  if (error_code != HS_SUCCESS) {
    throw std::runtime_error(std::string{"Error compiling pattern: "} +
                             compile_error->message);
  }

  auto database_error = hs_alloc_scratch(database, &scratch);
  if (database_error != HS_SUCCESS) {
    throw std::runtime_error("Error allocating scratch space");
  }
}

bool git_index_search::process_file(const char *filename,
                                    hs_scratch_t *local_scratch, char *buffer,
                                    std::string &lines) {
  int fd = open(filename, O_RDONLY, 0);
  if (fd == -1) {
    return false;
  }
  bool result{false};

  // Process the file in chunks
  std::size_t total_bytes_read = 0;
  bool max_file_size_provided = options.max_file_size.has_value();
  std::size_t max_file_size =
      max_file_size_provided ? options.max_file_size.value() : 0;
  std::size_t bytes_read = 0;
  std::atomic<std::size_t> max_line_number{0};
  std::size_t current_line_number{1};
  std::size_t num_matching_lines{0};

  // Read the file in chunks and perform search
  bool first{true};
  while (true) {

    bytes_read = read(fd, buffer, FILE_CHUNK_SIZE);

    if (bytes_read <= 0 || bytes_read > FILE_CHUNK_SIZE) {
      break;
    }

    total_bytes_read += bytes_read;

    if (max_file_size_provided && total_bytes_read > max_file_size) {
      // File size limit reached
      close(fd);
      return false;
    }

    if (first) {
      first = false;
      if (bytes_read >= 4 &&
          (is_elf_header(buffer) || is_archive_header(buffer))) {
        result = false;
        break;
      }

      if (has_null_bytes(buffer, bytes_read)) {
        // NULL bytes found
        // Ignore file
        // Could be a .exe, .gz, .bin etc.
        result = false;
        break;
      }
    }  

    // Find the position of the last newline in the buffer
    // In order to catch matches between chunks, need to amend the buffer
    // and make sure it stops at a new line boundary
    char *last_newline = (char *)memrchr(buffer, '\n', bytes_read);
    std::size_t search_size = bytes_read;
    if (last_newline) {
      search_size = last_newline - buffer;
    }

    std::mutex match_mutex;
    std::vector<std::pair<unsigned long long, unsigned long long>> matches{};
    std::atomic<size_t> number_of_matches = 0;
    file_context ctx{number_of_matches, matches, match_mutex,
                     options.print_only_filenames};

    if (hs_scan(database, buffer, search_size, 0, local_scratch, on_match,
                (void *)(&ctx)) != HS_SUCCESS) {
      if (options.print_only_filenames && ctx.number_of_matches > 0) {
        result = true;
      } else {
        result = false;
      }
      break;
    } else {
      if (ctx.number_of_matches > 0) {
        result = true;
      }
    }

    if (ctx.number_of_matches > 0) {
      process_matches(filename, buffer, search_size, ctx, current_line_number,
                      lines, true, options.is_stdout,
                      options.show_line_numbers);
      num_matching_lines += ctx.number_of_matches;
    }

    if (last_newline && bytes_read > search_size &&
        bytes_read == FILE_CHUNK_SIZE) /* Not the last chunk */ {

      if (static_cast<std::size_t>(last_newline - buffer) == bytes_read - 1) {
        // Chunk ends exactly at the newline
        // Do nothing
      } else {
        // Backtrack "remainder" number of characters
        if (bytes_read > search_size) {
          lseek(fd, -1 * (bytes_read - search_size), SEEK_CUR);
        }
      }
    }
  }

  close(fd);

  if (result && options.count_matching_lines) {
    if (options.is_stdout) {
      fmt::print("{}:{}\n",
                 fmt::format(fg(fmt::color::steel_blue), "{}", filename),
                 num_matching_lines);
    } else {
      fmt::print("{}:{}\n", filename, num_matching_lines);
    }
  } else {
    if (result && options.print_only_filenames) {
      if (options.is_stdout) {
        fmt::print(fg(fmt::color::steel_blue), "{}\n", filename);
      } else {
        fmt::print("{}\n", filename);
      }
    } else if (result && !lines.empty()) {
      if (options.is_stdout) {
        lines =
            fmt::format(fg(fmt::color::steel_blue), "\n{}\n", filename) + lines;
        fmt::print("{}", lines);
      } else {
        fmt::print("{}", lines);
      }
    }
  }

  lines.clear();
  return result;
}

bool git_index_search::search_submodules(const char *dir,
                                         git_repository *this_repo) {

  if (options.exclude_submodules) {
    return true;
  }

  struct context {
    git_index_search *ptr;
    const char *dir;
  };

  context ctx{this, dir};

  if (git_submodule_foreach(
          this_repo,
          [](git_submodule *sm, const char *name, void *payload) -> int {
            context *ctx = (context *)(payload);
            auto self = ctx->ptr;
            auto dir = ctx->dir;

            auto path = std::filesystem::path(dir);

            git_repository *sm_repo = nullptr;
            if (git_submodule_open(&sm_repo, sm) == 0) {
              auto submodule_path = path / name;
              self->visit_git_repo(std::move(submodule_path), sm_repo);
            }
            return 0;
          },
          (void *)&ctx) != 0) {
    return false;
  } else {
    return true;
  }
}

bool git_index_search::visit_git_index(const std::filesystem::path &dir,
                                       git_index *index) {
  git_index_iterator *iter{nullptr};
  if (git_index_iterator_new(&iter, index) == 0) {

    if (iter)
      garbage_collect_index_iterator.push_back(iter);

    const git_index_entry *entry = nullptr;
    while (git_index_iterator_next(&entry, iter) != GIT_ITEROVER) {
      if (entry && (!options.filter_files ||
                    (options.filter_files && filter_file(entry->path)))) {
        fmt::print("Enqueuing {}\n", entry->path);
        queue.enqueue(ptok, entry->path);
        ++num_files_enqueued;
      }
    }
    return true;
  } else {
    return false;
  }
}

bool git_index_search::visit_git_repo(const std::filesystem::path &dir,
                                      git_repository *repo) {

  bool result{true};

  // Open the repository
  if (!repo) {
    if (git_repository_open(&repo, dir.c_str()) != 0) {
      result = false;
    }
  }

  if (result)
    garbage_collect_repo.push_back(repo);

  // Load the git index for this repository
  git_index *index = nullptr;
  if (result && git_repository_index(&index, repo) != 0) {
    result = false;
  }

  if (result)
    garbage_collect_index.push_back(index);

  // Visit each entry in the index
  if (result && !visit_git_index(dir, index)) {
    result = false;
  }

  // Search the submodules inside this submodule
  if (result && !search_submodules(dir.c_str(), repo)) {
    result = false;
  }
  return result;
}

bool git_index_search::try_dequeue_and_process_path(hs_scratch_t *local_scratch,
                                                    char *buffer,
                                                    std::string &lines) {
  constexpr std::size_t BULK_DEQUEUE_SIZE = 32;
  const char* entries[BULK_DEQUEUE_SIZE];
  auto count =
      queue.try_dequeue_bulk_from_producer(ptok, entries, BULK_DEQUEUE_SIZE);
  if (count > 0) {
    for (std::size_t j = 0; j < count; ++j) {
      process_file(entries[j], local_scratch, buffer, lines);
    }
    num_files_dequeued += count;
    return true;
  }
  return false;
}

bool git_index_search::construct_file_filtering_hs_database() {
  hs_compile_error_t *compile_error = NULL;
  hs_error_t error_code =
      hs_compile(options.filter_file_pattern.c_str(), HS_FLAG_UTF8,
                 HS_MODE_BLOCK, NULL, &file_filter_database, &compile_error);
  if (error_code != HS_SUCCESS) {
    fprintf(stderr, "Error compiling pattern: %s\n", compile_error->message);
    hs_free_compile_error(compile_error);
    return false;
  }

  hs_error_t database_error =
      hs_alloc_scratch(file_filter_database, &file_filter_scratch);
  if (database_error != HS_SUCCESS) {
    fprintf(stderr, "Error allocating scratch space\n");
    hs_free_database(file_filter_database);
    return false;
  }

  return true;
}

int git_index_search::on_file_filter_match(unsigned int id,
                                           unsigned long long from,
                                           unsigned long long to,
                                           unsigned int flags, void *ctx) {
  filter_context *fctx = (filter_context *)(ctx);
  fctx->result = true;
  return HS_SUCCESS;
}

bool git_index_search::filter_file(const char *path) {
  filter_context ctx{false};
  if (hs_scan(file_filter_database, path, strlen(path), 0, file_filter_scratch,
              on_file_filter_match, (void *)(&ctx)) != HS_SUCCESS) {
    return true;
  }
  return ctx.result;
}

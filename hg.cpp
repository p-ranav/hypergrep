#include "concurrentqueue.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fmt/color.h>
#include <fmt/format.h>
#include <fstream>
#include <hs/hs.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <git2.h>

inline bool is_elf_header(const char *buffer)
{
    static constexpr std::string_view elf_magic = "\x7f"
                                                  "ELF";
    return (strncmp(buffer, elf_magic.data(), elf_magic.size()) == 0);
}

inline bool is_archive_header(const char *buffer)
{
    static constexpr std::string_view archive_magic = "!<arch>";
    return (strncmp(buffer, archive_magic.data(), archive_magic.size()) == 0);
}

moodycamel::ConcurrentQueue<std::string> queue;
moodycamel::ProducerToken                ptok(queue);

bool is_stdout{true};

std::atomic<bool>        path_is_a_git_repo{false};
std::atomic<bool>        git_index_built{false};
std::atomic<bool>        running{true};
std::atomic<std::size_t> num_files_enqueued{0};
std::atomic<std::size_t> num_files_dequeued{0};

constexpr std::size_t FILE_CHUNK_SIZE = 10 * 4096;

hs_database_t *           database = NULL;
hs_scratch_t *            scratch  = NULL;
std::vector<hs_scratch *> thread_local_scratch;
std::vector<hs_scratch *> thread_local_scratch_per_line;

bool option_show_line_numbers{false};
bool option_ignore_case{false};
bool option_search_only_git_index{false};

git_repository * repo = nullptr;
git_index * repo_index = nullptr;

struct file_context
{
    std::string &filename;
    const char * data;
    std::size_t &size;
    std::string &lines;
    std::size_t &current_line_number;
    const char **current_ptr;
    hs_scratch * local_scratch;
};

std::size_t count_newlines(const char *start, const char *end)
{
    if (end > start)
    {
        return std::count(start, end, '\n');
    }
    else
    {
        return 0;
    }
}

struct line_context
{
    const char * data;
    std::string &lines;
    const char **current_ptr;
};

static int print_match_in_red_color(unsigned int id, unsigned long long from, unsigned long long to, unsigned int flags, void *ctx)
{
    auto *       fctx      = static_cast<line_context *>(ctx);
    const char * line_data = fctx->data;
    auto &       lines     = fctx->lines;
    const char * start     = *(fctx->current_ptr);
    const size_t len       = to - from;

    lines.reserve(lines.size() + len + 9);
    lines.append(start, line_data + from);
    lines += fmt::format(fg(fmt::color::red), "{}", std::string_view(&line_data[from], len));
    *(fctx->current_ptr) = line_data + to;

    return 0;
}

static int on_match(unsigned int id, unsigned long long from, unsigned long long to, unsigned int flags, void *ctx)
{
    // print line with match
    auto *fctx = (file_context *)(ctx);

    auto &       lines               = fctx->lines;
    auto &       size                = fctx->size;
    std::size_t &current_line_number = fctx->current_line_number;

    if (memchr((void *)fctx->data, '\0', size) != NULL)
    {
        // NULL bytes found
        // Ignore file
        // Could be a .exe, .gz, .bin etc.
        return 1;
    }

    auto data = std::string_view(fctx->data, size);

    auto end = data.find_first_of('\n', to);
    if (end == std::string_view::npos)
    {
        end = size;
    }

    auto start = data.find_last_of('\n', from);
    if (start == std::string_view::npos)
    {
        start = 0;
    }
    else
    {
        start += 1;
    }

    auto line_count = 0;
    if (option_show_line_numbers)
    {
        const std::size_t previous_line_number = fctx->current_line_number;
        line_count                             = count_newlines(fctx->data, fctx->data + start);
        current_line_number += line_count;

        if (current_line_number == previous_line_number && previous_line_number > 0)
        {
            return 0;
        }
    }

    if (from >= start && to >= from && end >= to && end >= start)
    {
        if (is_stdout)
        {
            std::string_view line(&data[start], end - start);
            if (option_show_line_numbers)
            {
                lines += fmt::format(fg(fmt::color::green), "{}", current_line_number);
            }

            const char *line_ptr = line.data();

            line_context nested_ctx{line_ptr, lines, &line_ptr};
            if (hs_scan(database, &data[start], end - start, 0, fctx->local_scratch, print_match_in_red_color, &nested_ctx) != HS_SUCCESS)
            {
                return 1;
            }

            if (line_ptr != (&data[start] + end - start))
            {
                // some left over
                lines += std::string(line_ptr, &data[start] + end - start - line_ptr);
            }
            lines += '\n';
        }
        else
        {
            if (option_show_line_numbers)
            {
                lines += fmt::format("{}:{}:{}\n", fctx->filename, current_line_number, std::string_view(&data[start], end - start));
            }
            else
            {
                lines += fmt::format("{}:{}\n", fctx->filename, std::string_view(&data[start], end - start));
            }
        }
    }

    return 0;
}

template<std::size_t CHUNK_SIZE = FILE_CHUNK_SIZE>
bool process_file(std::string &&filename, std::size_t i, char *buffer, std::string &search_string, std::string &remainder_from_previous_chunk)
{
    int fd = open(filename.data(), O_RDONLY, 0);
    if (fd == -1)
    {
        return false;
    }
    bool result{true};

    // Set up the scratch space
    hs_scratch_t *local_scratch          = thread_local_scratch[i];
    hs_scratch_t *local_scratch_per_line = thread_local_scratch_per_line[i];

    // Process the file in chunks
    std::size_t bytes_read = 0;
    std::size_t current_line_number{1};
    std::string lines{""};

    // Read the file in chunks and perform search
    bool first{true};
    while ((bytes_read = read(fd, buffer, CHUNK_SIZE)) > 0)
    {
        if (first)
        {
            first = false;
            if (bytes_read >= 4 && (is_elf_header(buffer) || is_archive_header(buffer)))
            {
                result = false;
                break;
            }

            if (memchr((void *)buffer, '\0', bytes_read) != NULL)
            {
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
        char *      last_newline = (char *)memrchr(buffer, '\n', bytes_read);
        std::size_t search_size  = bytes_read;
        if (last_newline)
        {
            search_size = last_newline - buffer;
        }

        if (remainder_from_previous_chunk.empty())
        {
            // Process the current chunk
            file_context ctx{filename, buffer, search_size, lines, current_line_number, nullptr, local_scratch_per_line};
            if (hs_scan(database, buffer, search_size, 0, local_scratch, on_match, (void *)(&ctx)) != HS_SUCCESS)
            {
                result = false;
                break;
            }

            if (last_newline)
            {
                remainder_from_previous_chunk.append(last_newline, bytes_read - search_size);
            }
        }
        else
        {
            // If remaining bytes from previous chunk, prepend to the search buffer
            search_string = remainder_from_previous_chunk;
            search_string.append(buffer, search_size);
            search_size = search_string.size();
            remainder_from_previous_chunk.clear();

            // Process the current chunk along with the leftover from the previous chunk
            file_context ctx{filename, search_string.data(), search_size, lines, current_line_number, nullptr, local_scratch_per_line};
            if (hs_scan(database, search_string.data(), search_size, 0, local_scratch, on_match, (void *)(&ctx)) != HS_SUCCESS)
            {
                result = false;
                break;
            }

            if (last_newline)
            {
                remainder_from_previous_chunk.append(last_newline, bytes_read - (last_newline - buffer));
            }
        }
    }

    close(fd);

    if (result && !lines.empty())
    {
        if (is_stdout)
        {
            fmt::print("\n{}\n{}", filename, lines);
        }
        else
        {
            fmt::print("{}", lines);
        }
    }

    return result;
}

// void visit(std::string path)
// {
//     for (auto &&entry: std::filesystem::recursive_directory_iterator(path, std::filesystem::directory_options::skip_permission_denied))
//     {
//         const auto &path          = entry.path();
//         const auto &filename      = path.filename();
//         const auto  filename_cstr = filename.c_str();
//         const auto  pathstring    = path.string();
//         if (filename_cstr[0] == '.')
//             continue;

//         if (entry.is_regular_file())
//         {
//           queue.enqueue(ptok, std::move(pathstring));
//           num_files_enqueued += 1;
//         }
//     }
// }

void visit_git_index(std::string path)
{
  constexpr int                              BULK_ENQUEUE_SIZE = 32;
  std::array<std::string, BULK_ENQUEUE_SIZE> paths_to_enqueue;
  std::size_t i = 0;
  std::size_t index = 0;

  while (true)
  {
    const auto entry = git_index_get_byindex(repo_index, i++);
    if (!entry)
    {
      // No more entries
      break;
    }

    paths_to_enqueue[index++] = entry->path;

    if (index == BULK_ENQUEUE_SIZE)
    {
      queue.enqueue_bulk(ptok, std::make_move_iterator(paths_to_enqueue.begin()), BULK_ENQUEUE_SIZE);
      index = 0;
      num_files_enqueued += BULK_ENQUEUE_SIZE;
    }
  }

  if (index > 0)
  {
    queue.enqueue_bulk(ptok, paths_to_enqueue.begin(), index);
    num_files_enqueued += index;
  }
}

void visit(std::string path)
{
    constexpr int                              BULK_ENQUEUE_SIZE = 32;
    std::array<std::string, BULK_ENQUEUE_SIZE> paths_to_enqueue;

    std::size_t index{0};

    for (auto &&entry: std::filesystem::recursive_directory_iterator(path, std::filesystem::directory_options::skip_permission_denied))
    {
        const auto &path          = entry.path();
        const auto &filename      = path.filename();
        const auto  filename_cstr = filename.c_str();

        if (filename_cstr[0] == '.')
            continue;

        if (entry.is_regular_file())
        {
            // fmt::print("{} found\n", filename_cstr);
            const auto pathstring = path.string();
            paths_to_enqueue[index++] = std::move(pathstring);

            if (index == BULK_ENQUEUE_SIZE)
            {
              queue.enqueue_bulk(ptok, std::make_move_iterator(paths_to_enqueue.begin()), BULK_ENQUEUE_SIZE);
              index = 0;
              num_files_enqueued += BULK_ENQUEUE_SIZE;
            }
        }
    }
    if (index > 0)
    {
      queue.enqueue_bulk(ptok, paths_to_enqueue.begin(), index);
      num_files_enqueued += index;
    }
}

static inline bool visit_one(const std::size_t i, char *buffer, std::string &search_string, std::string &remaining_from_previous_chunk)
{
    constexpr std::size_t BULK_DEQUEUE_SIZE = 32;
    std::string           entries[BULK_DEQUEUE_SIZE];
    auto                  count = queue.try_dequeue_bulk_from_producer(ptok, entries, BULK_DEQUEUE_SIZE);
    if (count > 0)
    {
        for (std::size_t j = 0; j < count; ++j)
        {
            process_file(std::move(entries[j]), i, buffer, search_string, remaining_from_previous_chunk);
        }
        num_files_dequeued += count;
        return true;
    }

    return false;
}

int main(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "nig")) != -1)
    {
        switch (opt)
        {
        case 'n':
            option_show_line_numbers = true;
            break;
        case 'i':
            option_ignore_case = true;
            break;
        case 'g':
            option_search_only_git_index = true;
            break;
        default:
            fprintf(stderr, "Usage: %s [-n] [-i] [-g] pattern [filename]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    const char *pattern = argv[optind];
    const char *path    = ".";
    if (optind + 1 < argc)
    {
        path = argv[optind + 1];
    }

    std::thread git_repo_init_thread;
    if (option_search_only_git_index)
    {
      git_repo_init_thread = std::thread([&]() {
        git_libgit2_init();

        if (git_repository_open(&repo, path) == 0)
        {
          path_is_a_git_repo = true;
          if (git_repository_index(&repo_index, repo) == 0)
          {
            git_index_built = true;
          }
          else
          {
            fmt::print("Error: Failed to load repository index\n");
          }
        }
        else
        {
          fmt::print("Error: Not a git repo\n");
        }
      });      
    }

    is_stdout = isatty(STDOUT_FILENO) == 1;

    hs_compile_error_t *compile_error = NULL;
    hs_error_t          error_code    = hs_compile(pattern, (option_ignore_case ? HS_FLAG_CASELESS : 0) | HS_FLAG_UTF8 | HS_FLAG_SOM_LEFTMOST, HS_MODE_BLOCK, NULL, &database, &compile_error);
    if (error_code != HS_SUCCESS)
    {
        fprintf(stderr, "Error compiling pattern: %s\n", compile_error->message);
        hs_free_compile_error(compile_error);
        return 1;
    }

    hs_error_t database_error = hs_alloc_scratch(database, &scratch);
    if (database_error != HS_SUCCESS)
    {
        fprintf(stderr, "Error allocating scratch space\n");
        hs_free_database(database);
        return 1;
    }

    if (std::filesystem::is_regular_file(path))
    {
        // Set up the scratch space
        hs_scratch_t *local_scratch  = NULL;
        hs_error_t    database_error = hs_alloc_scratch(database, &local_scratch);
        if (database_error != HS_SUCCESS)
        {
            fprintf(stderr, "Error allocating scratch space\n");
            hs_free_database(database);
            return false;
        }
        thread_local_scratch.push_back(local_scratch);

        // Set up the scratch space per line
        hs_scratch_t *scratch_per_line = NULL;
        database_error                 = hs_alloc_scratch(database, &scratch_per_line);
        if (database_error != HS_SUCCESS)
        {
            fprintf(stderr, "Error allocating scratch space\n");
            hs_free_database(database);
            return false;
        }
        thread_local_scratch_per_line.push_back(scratch_per_line);

        std::string path_string(path);
        char        buffer[FILE_CHUNK_SIZE];
        std::string search_string{};
        std::string remaining_bytes_per_chunk{};
        process_file(std::move(path_string), 0, buffer, search_string, remaining_bytes_per_chunk);
    }
    else
    {
        const auto               N = std::thread::hardware_concurrency();
        std::vector<std::thread> consumer_threads(N);

        thread_local_scratch.reserve(N);
        thread_local_scratch_per_line.reserve(N);

        for (std::size_t i = 0; i < N; ++i)
        {
            // Set up the scratch space
            hs_scratch_t *local_scratch  = NULL;
            hs_error_t    database_error = hs_alloc_scratch(database, &local_scratch);
            if (database_error != HS_SUCCESS)
            {
                fprintf(stderr, "Error allocating scratch space\n");
                hs_free_database(database);
                return false;
            }
            thread_local_scratch.push_back(local_scratch);

            // Set up the scratch space per line
            hs_scratch_t *scratch_per_line = NULL;
            database_error                 = hs_alloc_scratch(database, &scratch_per_line);
            if (database_error != HS_SUCCESS)
            {
                fprintf(stderr, "Error allocating scratch space\n");
                hs_free_database(database);
                return false;
            }
            thread_local_scratch_per_line.push_back(scratch_per_line);

            consumer_threads[i] = std::thread([i = i]() {
                char        buffer[FILE_CHUNK_SIZE];
                std::string search_string{};
                std::string remaining_from_previous_chunk{};

                while (true)
                {
                    if (num_files_enqueued > 0)
                    {
                        visit_one(i, buffer, search_string, remaining_from_previous_chunk);
                        if (!running && num_files_dequeued == num_files_enqueued)
                        {
                            break;
                        }
                    }
                }
            });
        }

        if (option_search_only_git_index)
        {
          auto start = std::chrono::high_resolution_clock::now();
          git_repo_init_thread.join();
          auto end = std::chrono::high_resolution_clock::now();
          fmt::print("Wait time: {}\n", (std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()));
          if (path_is_a_git_repo && git_index_built)
          {
            visit_git_index(path);
          }
        }
        else
        {
          visit(path);
        }
        running = false;

        for (std::size_t i = 0; i < N; ++i)
        {
            consumer_threads[i].join();
        }

    }

    hs_free_scratch(scratch);
    hs_free_database(database);

    if (git_repo_init_thread.joinable())
    {
      git_repo_init_thread.join();
    }

    return 0;
}

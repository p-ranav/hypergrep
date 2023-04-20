#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <hs/hs.h>
#include <git2.h>
#include <stdlib.h>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <immintrin.h>
#include <atomic>

std::atomic_int is_binary{0};

constexpr int BLOCK_SIZE = 4096;

struct thread_args_t {
    const char* filename;
    std::size_t start;
    std::size_t end;
};

void* detect_binary_thread(void* arg) {
    auto args = static_cast<thread_args_t*>(arg);
    FILE* fp = fopen(args->filename, "rb");
    if (fp == nullptr) {
        return nullptr; // error opening file
    }
    fseek(fp, args->start, SEEK_SET);
    std::size_t block_size = (args->end - args->start) / BLOCK_SIZE * BLOCK_SIZE;
    std::size_t pos = args->start;
    __m256i zeroes = _mm256_setzero_si256();
    while (pos < args->start + block_size) {
        char buffer[BLOCK_SIZE];
        std::size_t read_size = fread(buffer, 1, BLOCK_SIZE, fp);
        if (read_size == 0) {
            break;
        }
        for (std::size_t i = 0; i < read_size; i += 32) {
            __m256i data = _mm256_loadu_si256((__m256i*)(buffer + i));
            __m256i cmp = _mm256_cmpgt_epi8(zeroes, data);
            int mask = _mm256_movemask_epi8(cmp);
            if (mask != 0) {
                if (!is_binary) {
                    is_binary.store(1);
                    return nullptr;
                }
                break;
            }
        }
        pos += read_size;
    }
    fclose(fp);
    return nullptr;
}

constexpr int NUM_THREADS = 8;

void is_binary_file(const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (fp == nullptr) {
        perror("fopen");
        exit(1);
    }
    fseek(fp, 0, SEEK_END);
    std::size_t file_size = ftell(fp);
    fclose(fp);

    pthread_t threads[NUM_THREADS];
    thread_args_t thread_args[NUM_THREADS];
    std::size_t block_size = file_size / NUM_THREADS;
    for (std::size_t i = 0; i < NUM_THREADS; i++) {
        thread_args[i].filename = filename;
        thread_args[i].start = i * block_size;
        thread_args[i].end = (i + 1) * block_size;
        pthread_create(&threads[i], nullptr, detect_binary_thread, &thread_args[i]);
    }
    for (std::size_t i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], nullptr);
    }
}

git_repository* repo = nullptr;

bool is_ignored(const char* path)
{
    if (!repo) {
        return false;
    }
    int ignored = 0; // 0 -> not ignored, 1 -> ignored
    if (git_ignore_path_is_ignored(&ignored, repo, path) < 0) {
        // failed to check if path is ignored
        return false;
    }

    return (bool)(ignored);
}

hs_database_t *database = NULL;
hs_scratch_t *scratch = NULL;

struct file_context {
    const char* filename;
    char* data;
    size_t size;
};

size_t file_num{0};
size_t current_file_num{0};

static int on_match(unsigned int id, unsigned long long from,
                       unsigned long long to, unsigned int flags, void *ctx)
{
    // print line with match
    auto* fctx = (file_context*)(ctx);
    auto filename = fctx->filename;
    auto data = fctx->data;
    auto size = fctx->size;
    auto start = to, end = to;
    while (start > 0 && data[start] != '\n') {
        start--;
    }
    while (end < size && data[end] != '\n') {
        end++;
    }

    if (data[start] == '\n' && start + 1 < end) {
        start += 1;
    }

    if (file_num > current_file_num)
    {
        // New file
        // Print filename
        std::cout << "\n" << filename << "\n";
        current_file_num = file_num;
    }

    if (end > start) {
        std::string_view line(&data[start], end - start);
        std::cout << line << "\n";
    }
    return 0;
}

char* buffer = NULL;
#define CHUNK_SIZE 1024 * 1024 // Define the size of each chunk to read

bool process_chunk(char* data, size_t size, const char* filename) {
    // Process the chunk of data here
    file_context ctx { filename, data, size };
    hs_scan(database, data, size, 0, scratch, on_match, (void *)(&ctx));

    return true;
}

int process_file(const char* filename) {
    ++file_num;

    FILE* file = fopen(filename, "rb"); // Open the file in binary mode
    if (file == NULL) {
        printf("Error opening file\n");
        return 1;
    }

    fseek(file, 0, SEEK_SET); // Set the file position indicator to the beginning of the file

    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, CHUNK_SIZE, file)) > 0) {
        process_chunk(buffer, bytes_read, filename); // Call the process function with the chunk of data
    }

    fclose(file); // Close the file

    return 0;
}

int visit(const char *path) {
    DIR *dir = opendir(path);
    if (dir == NULL) {
        // perror("opendir");
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {

        // Construct path
        const size_t path_len = strlen(path);
        const size_t name_len = _D_EXACT_NAMLEN(entry);
        const size_t total_len = path_len + 1 + name_len + 1;
        char filepath[total_len];
        memcpy(filepath, path, path_len);
        filepath[path_len] = '/';
        memcpy(filepath + path_len + 1, entry->d_name, name_len);
        filepath[total_len - 1] = '\0';

        // Check if path is a directory
        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            if (!is_ignored(filepath))
            {
                if (visit(filepath) == -1) {
                    closedir(dir);
                    return -1;
                }
            }
        }
        // Check if path is a regular file 
        else if (entry->d_type == DT_REG) {

            if (!is_ignored(filepath))
            {
                process_file(filepath);
            }
            // Open file
            // Search pattern with hs_scan
            // - If reading in chunks, handle partial matches
            // Close file
        }
    }

    closedir(dir);
    return 0;
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <directory>\n", argv[0]);
        return 1;
    }

    const char *path = argv[1];
    const char *pattern = argv[2];

    buffer = (char*) malloc(CHUNK_SIZE); // Allocate memory for the buffer

    char resolved_path[PATH_MAX]; 
    realpath(path, resolved_path); 

    git_libgit2_init();
    if (git_repository_open(&repo, resolved_path) < 0) {
        // failed to open repository
    }

    hs_compile_error_t *compile_error = NULL;
    hs_error_t error_code = hs_compile(pattern, 0, HS_MODE_BLOCK, NULL, &database, &compile_error);
    if (error_code != HS_SUCCESS)
    {
        fprintf(stderr, "Error compiling pattern: %s\n", compile_error->message);
        hs_free_compile_error(compile_error);
        return 1;
    }

    // Set up the scratch space
    hs_error_t database_error = hs_alloc_scratch(database, &scratch);
    if (database_error != HS_SUCCESS)
    {
        fprintf(stderr, "Error allocating scratch space\n");
        hs_free_database(database);
        return 1;
    }

    if (visit(resolved_path) == -1) {
        return 1;
    }

    free(buffer); // Free the buffer memory


    git_repository_free(repo);

    return 0;
}
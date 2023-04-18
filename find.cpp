#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <hs/hs.h>

hs_database_t *database = NULL;
hs_scratch_t *scratch = NULL;

static int on_match(unsigned int id, unsigned long long from,
                       unsigned long long to, unsigned int flags, void *ctx)
{
    puts((const char*)ctx);
    return 0;
}

int visit(const char *path) {
    struct dirent **entries;
    int num_entries = scandir(path, &entries, NULL, NULL);
    if (num_entries == -1) {
        perror("scandir");
        return -1;
    }

    for (int i = 0; i < num_entries; i++) {
        struct dirent *entry = entries[i];
        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name);

        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            if (visit(filepath) == -1) {
                return -1;
            }
        } else if (entry->d_type == DT_REG) {
            hs_scan(database, filepath, strlen(filepath), 0, scratch, on_match, (void *)filepath);
        }

        free(entry);
    }

    free(entries);
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

    if (visit(path) == -1) {
        return 1;
    }

    return 0;
}

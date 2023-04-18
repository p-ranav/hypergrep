#include <iostream>
#include <cstring>
#include <hs/hs.h>
#include <filesystem>

static int on_match(unsigned int id, unsigned long long from,
                       unsigned long long to, unsigned int flags, void *ctx)
{
    puts((const char*)ctx);
    putc('\n', stdout);
    return 0;
}

static void search_directory_recursive(const char* directory, hs_database_t* database, hs_scratch_t* scratch)
{
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied))
    {
        hs_scan(database, entry.path().c_str(), strlen(entry.path().c_str()), 0, scratch, on_match, (void *)entry.path().c_str());
    }
}

int main(int argc, char **argv)
{
    const char *path = argv[1];
    const char *pattern = argv[2];

    hs_database_t *database = nullptr;
    hs_compile_error_t *compile_error = nullptr;
    hs_error_t error_code = hs_compile(pattern, 0, HS_MODE_BLOCK, nullptr, &database, &compile_error);
    if (error_code != HS_SUCCESS)
    {
        std::cerr << "Error compiling pattern: " << compile_error->message << "\n";
        hs_free_compile_error(compile_error);
        return 1;
    }

    // Set up the scratch space
    hs_scratch_t *scratch = nullptr;
    hs_error_t database_error = hs_alloc_scratch(database, &scratch);
    if (database_error != HS_SUCCESS)
    {
        std::cerr << "Error allocating scratch space" << "\n";
        hs_free_database(database);
        return 1;
    }

    search_directory_recursive(path, database, scratch);

    hs_free_scratch(scratch);
    hs_free_database(database);
}

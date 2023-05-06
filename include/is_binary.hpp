#pragma once
#include <cstring>
#include <string_view>

bool is_elf_header(const char *buffer);

bool is_archive_header(const char *buffer);

bool has_null_bytes(char* buffer, std::size_t search_size);

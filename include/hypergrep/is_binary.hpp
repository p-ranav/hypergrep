#pragma once
#include <cstring>
#include <string_view>

bool is_elf_header(const char *buffer);

bool is_archive_header(const char *buffer);

bool is_jpeg(const char *buffer);

bool is_png(const char *buffer);

bool is_zip(const char *buffer);

bool is_gzip(const char *buffer);

bool is_tar(const char *buffer, const std::size_t &bytes_read);

bool is_pdf(const char *buffer);

bool starts_with_magic_bytes(const char *buffer, const std::size_t &bytes_read);

bool has_null_bytes(char *buffer, std::size_t search_size);

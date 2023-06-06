#pragma once
#include <string_view>

constexpr static inline std::string_view NAME = "hg";
constexpr static inline std::string_view DESCRIPTION =
    "Recursively search directories for a regex pattern";
constexpr static inline std::string_view VERSION = "0.1.1";
constexpr static inline std::size_t TYPICAL_FILESYSTEM_BLOCK_SIZE = 4096;
constexpr static inline std::size_t FILE_CHUNK_SIZE =
    16 * TYPICAL_FILESYSTEM_BLOCK_SIZE;
constexpr static inline std::size_t LARGE_FILE_SIZE = 1024 * 1024;
constexpr static inline std::size_t MAX_LINE_LENGTH = 4096;
constexpr static inline std::string_view WHITESPACE = " \t";

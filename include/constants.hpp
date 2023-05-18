#pragma once

constexpr static inline std::size_t TYPICAL_FILESYSTEM_BLOCK_SIZE = 4096;
constexpr static inline std::size_t FILE_CHUNK_SIZE = 
    16 * TYPICAL_FILESYSTEM_BLOCK_SIZE;
constexpr static inline std::size_t LARGE_FILE_SIZE = 1024 * 1024;

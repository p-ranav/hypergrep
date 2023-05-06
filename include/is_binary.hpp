#pragma once
#include <cstring>
#include <string_view>

static inline bool is_elf_header(const char *buffer) {
  static constexpr std::string_view elf_magic = "\x7f"
                                                "ELF";
  return (strncmp(buffer, elf_magic.data(), elf_magic.size()) == 0);
}

static inline bool is_archive_header(const char *buffer) {
  static constexpr std::string_view archive_magic = "!<arch>";
  return (strncmp(buffer, archive_magic.data(), archive_magic.size()) == 0);
}

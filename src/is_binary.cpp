#include <is_binary.hpp>

bool is_elf_header(const char *buffer) {
  static constexpr std::string_view elf_magic = "\x7f"
                                                "ELF";
  return (strncmp(buffer, elf_magic.data(), elf_magic.size()) == 0);
}

bool is_archive_header(const char *buffer) {
  static constexpr std::string_view archive_magic = "!<arch>";
  return (strncmp(buffer, archive_magic.data(), archive_magic.size()) == 0);
}

bool has_null_bytes(char *buffer, std::size_t search_size) {
  if (memchr((void *)buffer, '\0', search_size) != NULL) {
    return true;
  }
  return false;
}

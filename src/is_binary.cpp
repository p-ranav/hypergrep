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

bool is_jpeg(const char *buffer) {
  static constexpr std::string_view jpg_magic = "\xFF\xD8\xFF";
  return (strncmp(buffer, jpg_magic.data(), jpg_magic.size()) == 0);
}

bool is_png(const char *buffer) {
  static constexpr std::string_view png_magic = "\x89PNG\r\n\x1A\n";
  return (strncmp(buffer, png_magic.data(), png_magic.size()) == 0);
}

bool is_zip(const char *buffer) {
  static constexpr std::string_view zip_magic = "PK\x03\x04";
  return (strncmp(buffer, zip_magic.data(), zip_magic.size()) == 0);
}

bool is_gzip(const char *buffer) {
  static constexpr std::string_view gzip_magic = "\x1F\x8B";
  return (strncmp(buffer, gzip_magic.data(), gzip_magic.size()) == 0);
}

bool is_tar(const char *buffer, const std::size_t &bytes_read) {
  // Check for:
  //
  // 75 73 74 61 72 00 30 30
  // or
  // 75 73 74 61 72 20 20 00
  //
  // at byte offset 257
  static constexpr std::string_view tar_magic_1 =
      "75\x73\x74\x61\x72\x00\x30\x30";
  static constexpr std::string_view tar_magic_2 =
      "75\x73\x74\x61\x72\x20\x20\x00";

  if (bytes_read < (257 + tar_magic_1.size())) {
    // not enough bytes
    return false;
  }

  return (strncmp(buffer + 257, tar_magic_1.data(), tar_magic_1.size()) == 0) ||
         (strncmp(buffer + 257, tar_magic_2.data(), tar_magic_2.size()) == 0);
}

bool is_pdf(const char *buffer) {
  // 25 50 44 46 2D
  static constexpr std::string_view pdf_magic = "\x25\x50\x44\x46\x2D";
  return (strncmp(buffer, pdf_magic.data(), pdf_magic.size()) == 0);
}

bool starts_with_magic_bytes(const char *buffer,
                             const std::size_t &bytes_read) {
  return is_elf_header(buffer) || is_archive_header(buffer);
}

bool has_null_bytes(char *buffer, std::size_t search_size) {
  if (memchr((void *)buffer, '\0', search_size) != NULL) {
    return true;
  }
  return false;
}

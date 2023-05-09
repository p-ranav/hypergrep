#include <algorithm>
#include <cmath>
#include <size_to_bytes.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

// Function to convert string to bytes
unsigned long long size_to_bytes(std::string_view input) {
  // Check if the input string is empty
  if (input.empty()) {
    throw std::runtime_error("Invalid size requirement");
  }

  // Get the numeric part of the string
  std::size_t multiplier_pos = input.find_last_of("KMGTPEkmgtpe");
  if (multiplier_pos == std::string_view::npos) {
    throw std::runtime_error("Invalid size requirement " + std::string(input));
  }

  std::string_view numeric_part = input.substr(0, multiplier_pos);
  std::string_view multiplier_part = input.substr(multiplier_pos);

  // Check if the numeric part is a valid number
  unsigned long long num = 0;
  try {
    num = std::stoull(numeric_part.data());
  } catch (const std::exception &e) {
    throw std::runtime_error("Invalid numeric part " +
                             std::string(numeric_part));
  }

  // Get the multiplier character (last character of the string)
  char multiplier = std::tolower(multiplier_part.back());

  // Define a map for multiplier characters to corresponding power of 1024
  static const std::unordered_map<char, int> multipliers{
      {'k', 1}, {'m', 2}, {'g', 3}, {'t', 4}, {'p', 5}, {'e', 6}};

  // Check if the multiplier character is valid
  if (multipliers.find(multiplier) == multipliers.end()) {
    throw std::runtime_error("Invalid multiplier character " +
                             std::string(multiplier_part));
  }

  // Calculate the equivalent number of bytes using std::pow
  unsigned long long bytes = num * std::pow(1024, multipliers.at(multiplier));

  return bytes;
}

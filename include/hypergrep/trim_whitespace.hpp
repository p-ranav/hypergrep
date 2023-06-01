#pragma once
#include <cctype>
#include <hypergrep/constants.hpp>
#include <string>
#include <string_view>

std::string_view ltrim(const std::string_view &s);

std::string_view rtrim(const std::string_view &s);

std::string_view trim(const std::string_view &s);
cmake_minimum_required(VERSION 3.14)
project(find)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Release)
add_compile_options(-march=native -O3)

include(FetchContent)

# Fetch and include the fmt library
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG        9.1.0
)
FetchContent_MakeAvailable(fmt)

add_executable(hg hg.cpp)
target_include_directories(hg PRIVATE ${fmt_SOURCE_DIR}/include)
target_link_libraries(hg PRIVATE hs pthread fmt::fmt)

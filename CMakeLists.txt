cmake_minimum_required(VERSION 3.16)

project(hypergrep VERSION 0.1.0
        DESCRIPTION "Recursively search directories for a regex pattern"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Release)
add_compile_options(-march=native -O3 -Wall -Wpedantic)

# ———————————————————————————
# find 3rd party dependencies
# ———————————————————————————

find_package(unofficial-concurrentqueue CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(unofficial-git2 CONFIG REQUIRED)
find_package(argparse CONFIG REQUIRED)

find_path(HYPERSCAN_INCLUDE_DIR hs/hs.h)
find_library(HYPERSCAN_LIBRARY hs)

message(STATUS ${HYPERSCAN_INCLUDE_DIR})
message(STATUS ${HYPERSCAN_LIBRARY})

# —————————————————————————————
# create target for main binary
# —————————————————————————————
add_executable(hg
  src/is_binary.cpp
  src/directory_search.cpp
  src/file_search.cpp
  src/filter_file.cpp
  src/git_index_search.cpp
  src/match_handler.cpp
  src/main.cpp
  src/print_help.cpp
  src/size_to_bytes.cpp)
target_compile_features(hg PUBLIC cxx_std_17)
target_include_directories(hg PRIVATE include)
target_link_libraries(hg PRIVATE
  argparse::argparse
  unofficial::concurrentqueue::concurrentqueue
  unofficial::git2::git2
  fmt::fmt
  ${HYPERSCAN_LIBRARY}
  )


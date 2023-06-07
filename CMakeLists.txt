cmake_minimum_required(VERSION 3.16)

project(hypergrep VERSION 0.1.1
        DESCRIPTION "Recursively search directories for a regex pattern"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Release)
add_compile_options(-march=x86-64 -mtune=generic -O3 -Wall -Wpedantic)

# ———————————————————————————
# find 3rd party dependencies
# ———————————————————————————

find_package(unofficial-concurrentqueue CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(unofficial-git2 CONFIG REQUIRED)
find_package(argparse CONFIG REQUIRED)

find_path(HYPERSCAN_INCLUDE_DIR hs/hs.h)
find_library(HYPERSCAN_LIBRARY hs RELEASE)

message(STATUS ${HYPERSCAN_INCLUDE_DIR})
message(STATUS ${HYPERSCAN_LIBRARY})

# —————————————————————————————
# create target for main binary
# —————————————————————————————
add_executable(hg
  src/compiler.cpp
  src/cpu_features.cpp  
  src/is_binary.cpp
  src/directory_search.cpp
  src/file_filter.cpp
  src/file_search.cpp
  src/git_index_search.cpp
  src/match_handler.cpp
  src/main.cpp
  src/print_help.cpp
  src/search_options.cpp
  src/size_to_bytes.cpp
  src/trim_whitespace.cpp)
target_compile_features(hg PUBLIC cxx_std_17)
target_include_directories(hg PRIVATE include)
target_link_libraries(hg PRIVATE
  argparse::argparse
  unofficial::concurrentqueue::concurrentqueue
  unofficial::git2::libgit2package
  fmt::fmt
  ${HYPERSCAN_LIBRARY}
  )

install(TARGETS hg
DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)

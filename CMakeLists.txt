cmake_minimum_required(VERSION 3.16)
project(hg)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Release)
add_compile_options(-march=native -O3)

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
add_executable(hg src/hg.cpp)
target_compile_features(hg PUBLIC cxx_std_17)
target_include_directories(hg PRIVATE include)
target_link_libraries(hg PRIVATE
  argparse::argparse
  unofficial::concurrentqueue::concurrentqueue
  unofficial::git2::git2
  fmt::fmt
  ${HYPERSCAN_LIBRARY}
  )


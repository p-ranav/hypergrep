cmake_minimum_required(VERSION 3.14)
project(find)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Release)
add_compile_options(-march=native -O3 -fopenmp)

include(FetchContent)

# Fetch and include the fmt library
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG        9.1.0
)
FetchContent_MakeAvailable(fmt)

add_executable(find find.cpp)
target_link_libraries(find PRIVATE hs)

add_executable(search search.cpp)
target_link_libraries(search PRIVATE hs pthread git2)

# Check if population has already been performed
FetchContent_GetProperties(fmt)
if(NOT fmt_POPULATED)
  # Fetch the content using previously declared details
  FetchContent_Populate(fmt)
endif()

# Find OpenMP
find_package(OpenMP REQUIRED)

# Add the OpenMP flag to the compiler command line
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")

add_executable(hg hg.cpp)
target_include_directories(hg PRIVATE ${fmt_SOURCE_DIR}/include)
target_link_libraries(hg PRIVATE hs pthread fmt::fmt OpenMP::OpenMP_CXX)

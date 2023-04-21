cmake_minimum_required(VERSION 3.14)
project(find)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Release)
add_compile_options(-march=native -O3)

add_executable(find find.cpp)
target_link_libraries(find PRIVATE hs)

add_executable(search search.cpp)
target_link_libraries(search PRIVATE hs pthread git2)

add_executable(hg hg.cpp)
target_link_libraries(hg PRIVATE hs pthread git2)
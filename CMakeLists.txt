cmake_minimum_required(VERSION 3.18)
project(VideoDLBackend VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(videodl-api backend/main.cpp)
target_include_directories(videodl-api PRIVATE ${CMAKE_SOURCE_DIR}/vendor)
target_link_libraries(videodl-api PRIVATE Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(videodl-api PRIVATE -O2 -Wall -Wextra -Wpedantic)
endif()

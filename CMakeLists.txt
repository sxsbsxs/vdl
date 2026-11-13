cmake_minimum_required(VERSION 3.20)
project(VideoDL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
find_package(Threads REQUIRED)
find_package(nlohmann_json 3.11 REQUIRED)

FetchContent_Declare(
  cpp_httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.27.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(cpp_httplib)

add_executable(vdl-server backend/main.cpp)
target_link_libraries(vdl-server PRIVATE
  httplib::httplib
  nlohmann_json::nlohmann_json
  Threads::Threads
)
target_compile_options(vdl-server PRIVATE -Wall -Wextra -Wpedantic)

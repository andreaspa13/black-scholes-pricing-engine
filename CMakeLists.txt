cmake_minimum_required(VERSION 3.16)
project(OptionsEngine VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler warnings ─────────────────────────────────────────────────────────
# Treat warnings as signal, not errors, during development.
# In a CI pipeline you'd add -Werror.
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
)

# ── Include directories ───────────────────────────────────────────────────────
include_directories(include)

# ── Library: core pricing engine ─────────────────────────────────────────────
add_library(options_engine
    src/options/EuropeanOption.cpp
    src/models/BlackScholesModel.cpp
    src/models/MonteCarloModel.cpp
)

target_include_directories(options_engine PUBLIC include)

# ── Executable: main benchmark + sanity check ─────────────────────────────────
add_executable(options_main src/main.cpp)
target_link_libraries(options_main PRIVATE options_engine)

# ── Build type defaults ───────────────────────────────────────────────────────
# Default to Release for benchmarking. Use -DCMAKE_BUILD_TYPE=Debug for
# debugging or Valgrind runs (Debug disables optimisations).
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# ── Executable: HTTP pricing server ──────────────────────────────────────────
add_executable(options_server src/server.cpp)
target_include_directories(options_server PRIVATE include)
target_link_libraries(options_server PRIVATE options_engine)
if(WIN32)
    target_link_libraries(options_server PRIVATE ws2_32 -lpthread)
endif()

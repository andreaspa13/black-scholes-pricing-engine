cmake_minimum_required(VERSION 3.16)
project(OptionsEngine VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
)

include_directories(include)

# Library: core pricing engine (compiled as a static library)
add_library(options_engine
    src/options/EuropeanOption.cpp
    src/models/BlackScholesModel.cpp
    src/models/MonteCarloModel.cpp
    src/utils/ImpliedVol.cpp
)

target_include_directories(options_engine PUBLIC include) # Makes include available to options_engine links 

# Executable: main application for testing the pricing engine
add_executable(options_main src/main.cpp) # Compile main.cpp into an executable named options_main
target_link_libraries(options_main PRIVATE options_engine)
if(WIN32)
    target_link_libraries(options_main PRIVATE -lpthread) #V3 uses std::thread
endif()

# Build type defaults
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

#  Executable: HTTP pricing server 
add_executable(options_server src/server.cpp)
target_include_directories(options_server PRIVATE include)
target_link_libraries(options_server PRIVATE options_engine)
if(WIN32)
    target_link_libraries(options_server PRIVATE ws2_32 -lpthread)
endif()

cmake_minimum_required(VERSION 3.14)
project(LaserTrackerHSM
    VERSION 1.0.0
    DESCRIPTION "HSM State Pattern Demo using std::variant for Laser Tracker"
    LANGUAGES CXX
)

# Require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable warnings
if(MSVC)
    add_compile_options(/W4 /WX-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Main executable
add_executable(laser_tracker_hsm
    main.cpp
)

# Set output directory
set_target_properties(laser_tracker_hsm PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Print configuration info
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")

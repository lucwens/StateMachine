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
    # Remove default /W3 and /WX to avoid D9025 override warnings
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "/WX-?" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Ensure debug builds have no optimizations for proper debugging
if(MSVC)
    # /Od = disable optimizations, /Zi = full debug info, /RTC1 = runtime checks
    # /MDd = debug dynamic runtime library (required for debug CRT symbols)
    set(CMAKE_CXX_FLAGS_DEBUG "/MDd /Od /Zi /RTC1 /DDEBUG /D_DEBUG")
else()
    # -O0 = no optimization, -g = debug symbols, -fno-omit-frame-pointer = better stack traces
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -fno-omit-frame-pointer -DDEBUG")
endif()

# Find threading library
find_package(Threads REQUIRED)

# Fetch dependencies
include(FetchContent)

# nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
# Prevent overriding parent project's compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Main executable
add_executable(laser_tracker_hsm
    main.cpp
)

# Link libraries
target_link_libraries(laser_tracker_hsm PRIVATE
    Threads::Threads
    nlohmann_json::nlohmann_json
)

# Set output directory (flatten config subdirectory for multi-config generators)
set_target_properties(laser_tracker_hsm PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin"
)

# Enable testing
enable_testing()

# Test executable
add_executable(hsm_tests
    tests/test_hsm.cpp
    tests/test_threaded_hsm.cpp
    tests/test_action_commands.cpp
)

target_link_libraries(hsm_tests PRIVATE
    Threads::Threads
    nlohmann_json::nlohmann_json
    GTest::gtest_main
)

# Set output directory for tests
set_target_properties(hsm_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin"
)

include(GoogleTest)
gtest_discover_tests(hsm_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES TIMEOUT 120
)

# Print configuration info
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")

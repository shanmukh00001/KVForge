cmake_minimum_required(VERSION 3.16)
project(KVForge VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Strict warning flags to ensure clean, high-quality code
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# Build options
option(ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ENABLE_COVERAGE "Build with code coverage analysis" OFF)
option(ENABLE_FUZZING "Build libFuzzer harnesses" OFF)

if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if(ENABLE_COVERAGE)
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

if(ENABLE_FUZZING)
    add_compile_options(-fsanitize=fuzzer,address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=fuzzer,address)
endif()

# Include directories: lets us #include "config/config.hpp" easily
include_directories(src)

# Core library target
add_library(kvforge_core
    src/util/log.cpp
    src/config/config.cpp
    src/net/socket_util.cpp
    src/net/connection.cpp
    src/net/listener.cpp
    src/net/event_loop.cpp
    src/protocol/resp_writer.cpp
    src/protocol/resp_parser.cpp
    src/commands/command_table.cpp
    src/commands/server_cmds.cpp
    src/store/hash_table.cpp
    src/commands/string_cmds.cpp
    src/util/int_parse.cpp
    src/util/glob.cpp
    src/expiry/expiry_index.cpp
    src/commands/key_cmds.cpp
    src/persistence/aof.cpp
)

# Main executable target
add_executable(kvforge
    src/main.cpp
)
target_link_libraries(kvforge PRIVATE kvforge_core)

# Fuzzing targets
if(ENABLE_FUZZING)
    add_executable(fuzz_resp_parser tests/fuzz/fuzz_resp_parser.cpp)
    target_link_libraries(fuzz_resp_parser PRIVATE kvforge_core)

    add_executable(fuzz_aof_loader tests/fuzz/fuzz_aof_loader.cpp)
    target_link_libraries(fuzz_aof_loader PRIVATE kvforge_core)
endif()

# -------------------------------------------------------------
# GoogleTest and Automated Testing
# -------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# For Windows/MSVC compatibility
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
enable_testing()
add_executable(kvforge_tests
    tests/unit/test_config.cpp
    tests/unit/test_net.cpp
    tests/unit/test_resp.cpp
    tests/unit/test_store.cpp
    tests/unit/test_int.cpp
    tests/unit/test_expiry.cpp
    tests/unit/test_limits.cpp
    tests/unit/test_rehash.cpp
    tests/unit/test_aof.cpp
    tests/unit/test_keys_info.cpp
)
target_link_libraries(kvforge_tests PRIVATE kvforge_core GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(kvforge_tests)

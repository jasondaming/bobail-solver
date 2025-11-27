cmake_minimum_required(VERSION 3.16)
project(bobail-solver VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# Find dependencies
find_package(GTest REQUIRED)

# RocksDB via pkg-config (for storage layer - optional for now)
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(ROCKSDB rocksdb)
endif()

# Main library (engine code)
add_library(bobail_engine
    src/board.cpp
    src/movegen.cpp
    src/hash.cpp
    src/symmetry.cpp
    src/tt.cpp
    src/pns.cpp
)
target_include_directories(bobail_engine PUBLIC include)
if(ROCKSDB_FOUND)
    target_include_directories(bobail_engine PUBLIC ${ROCKSDB_INCLUDE_DIRS})
    target_link_libraries(bobail_engine PUBLIC ${ROCKSDB_LIBRARIES})
    target_compile_definitions(bobail_engine PUBLIC HAS_ROCKSDB)
endif()

# Main solver executable
add_executable(bobail_solver src/main.cpp)
target_link_libraries(bobail_solver PRIVATE bobail_engine)

# Perft validation tool
add_executable(perft src/perft.cpp)
target_link_libraries(perft PRIVATE bobail_engine)

# Tests
enable_testing()
add_executable(bobail_tests
    tests/test_board.cpp
    tests/test_movegen.cpp
    tests/test_symmetry.cpp
)
target_link_libraries(bobail_tests PRIVATE bobail_engine GTest::gtest_main)
add_test(NAME bobail_tests COMMAND bobail_tests)

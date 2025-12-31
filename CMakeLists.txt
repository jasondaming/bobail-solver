cmake_minimum_required(VERSION 3.16)
project(bobail-solver VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BOBAIL_BUILD_TESTS "Build test targets" OFF)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# Find dependencies
if(BOBAIL_BUILD_TESTS)
    find_package(GTest REQUIRED)
endif()

# RocksDB - try pkg-config first, then direct library search
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(ROCKSDB rocksdb)
endif()

# If pkg-config didn't find it, try direct search
if(NOT ROCKSDB_FOUND)
    find_library(ROCKSDB_LIB rocksdb)
    find_path(ROCKSDB_INCLUDE rocksdb/db.h)
    if(ROCKSDB_LIB AND ROCKSDB_INCLUDE)
        set(ROCKSDB_FOUND TRUE)
        set(ROCKSDB_LIBRARIES ${ROCKSDB_LIB})
        set(ROCKSDB_INCLUDE_DIRS ${ROCKSDB_INCLUDE})
        message(STATUS "Found RocksDB via direct search: ${ROCKSDB_LIB}")
    endif()
endif()

# Main library (engine code)
add_library(bobail_engine
    src/board.cpp
    src/movegen.cpp
    src/hash.cpp
    src/symmetry.cpp
    src/tt.cpp
    src/pns.cpp
    src/retrograde.cpp
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

# Retrograde solver (strong solution)
add_executable(retrograde_solve src/retrograde_main.cpp)
target_link_libraries(retrograde_solve PRIVATE bobail_engine)

# Disk-based retrograde solver (using RocksDB)
if(ROCKSDB_FOUND)
    add_executable(retrograde_db
        src/retrograde_db.cpp
        src/retrograde_db_main.cpp
    )
    target_include_directories(retrograde_db PRIVATE include ${ROCKSDB_INCLUDE_DIRS})
    target_link_libraries(retrograde_db PRIVATE bobail_engine ${ROCKSDB_LIBRARIES})
    message(STATUS "Building disk-based retrograde solver with RocksDB")

    # Practical playing engine with PNS data
    add_executable(bobail_play
        src/engine.cpp
    )
    target_include_directories(bobail_play PRIVATE include)
    target_link_libraries(bobail_play PRIVATE bobail_engine)

    # Opening book exporter
    add_executable(export_book
        src/retrograde_db.cpp
        src/export_book.cpp
    )
    target_include_directories(export_book PRIVATE include ${ROCKSDB_INCLUDE_DIRS})
    target_link_libraries(export_book PRIVATE bobail_engine ${ROCKSDB_LIBRARIES})

    # Database lookup tool
    add_executable(lookup
        src/retrograde_db.cpp
        src/lookup.cpp
    )
    target_include_directories(lookup PRIVATE include ${ROCKSDB_INCLUDE_DIRS})
    target_link_libraries(lookup PRIVATE bobail_engine ${ROCKSDB_LIBRARIES})

    # Endgame analysis tool
    add_executable(analyze_endgames
        src/retrograde_db.cpp
        src/analyze_endgames.cpp
    )
    target_include_directories(analyze_endgames PRIVATE include ${ROCKSDB_INCLUDE_DIRS})
    target_link_libraries(analyze_endgames PRIVATE bobail_engine ${ROCKSDB_LIBRARIES})

    # Enhanced PNS solver with checkpoint and retrograde DB integration
    add_executable(pns_enhanced
        src/retrograde_db.cpp
        src/pns_enhanced.cpp
    )
    target_include_directories(pns_enhanced PRIVATE include ${ROCKSDB_INCLUDE_DIRS})
    target_link_libraries(pns_enhanced PRIVATE bobail_engine ${ROCKSDB_LIBRARIES})

    # Verification tool for PNS results
    add_executable(verify_wins
        src/verify_wins.cpp
    )
    target_include_directories(verify_wins PRIVATE include)
    target_link_libraries(verify_wins PRIVATE bobail_engine)

    # Trace winning lines
    add_executable(trace_win
        src/trace_win.cpp
    )
    target_include_directories(trace_win PRIVATE include)
    target_link_libraries(trace_win PRIVATE bobail_engine)

    add_executable(trace_alt
        src/trace_alt.cpp
    )
    target_include_directories(trace_alt PRIVATE include)
    target_link_libraries(trace_alt PRIVATE bobail_engine)

    add_executable(verify_all_responses
        src/verify_all_responses.cpp
    )
    target_include_directories(verify_all_responses PRIVATE include)
    target_link_libraries(verify_all_responses PRIVATE bobail_engine)

    # Position visualization URL generator
    add_executable(visualize
        src/visualize.cpp
    )
    target_include_directories(visualize PRIVATE include)
    target_link_libraries(visualize PRIVATE bobail_engine)

    # Export proved positions from PNS checkpoint
    add_executable(pns_export
        src/pns_export.cpp
    )
    target_include_directories(pns_export PRIVATE include)
    target_link_libraries(pns_export PRIVATE bobail_engine)

    # PNS checkpoint lookup tool
    add_executable(pns_lookup
        src/pns_lookup.cpp
    )
    target_include_directories(pns_lookup PRIVATE include)
    target_link_libraries(pns_lookup PRIVATE bobail_engine)
else()
    message(WARNING "RocksDB not found - disk-based retrograde solver will not be built")
endif()

if(BOBAIL_BUILD_TESTS)
    enable_testing()
    add_executable(bobail_tests
        tests/test_board.cpp
        tests/test_movegen.cpp
        tests/test_symmetry.cpp
    )
    target_link_libraries(bobail_tests PRIVATE bobail_engine GTest::gtest_main)
    add_test(NAME bobail_tests COMMAND bobail_tests)

    add_executable(solver_correctness_tests
        tests/test_solver_correctness.cpp
    )
    target_link_libraries(solver_correctness_tests PRIVATE bobail_engine GTest::gtest_main)
    add_test(NAME solver_correctness_tests COMMAND solver_correctness_tests)
endif()

cmake_minimum_required(VERSION 3.15...3.27)
project(chessmimic_core)

# ==============================================================================
# Global settings and configurations
# ==============================================================================

# Default to Release build for better performance
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# C++ settings
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================
# Find Python and prepare development environment
# ==============================================================================

if (CMAKE_VERSION VERSION_LESS 3.18)
  set(DEV_MODULE Development)
else()
  set(DEV_MODULE Development.Module)
endif()

# Auto-detect the repo's virtual environment when an IDE (e.g. CLion) invokes
# CMake without activating it. Honors an explicitly provided Python_EXECUTABLE
# and an active VIRTUAL_ENV, otherwise falls back to ../.venv.
if (NOT DEFINED Python_EXECUTABLE AND NOT DEFINED CACHE{Python_EXECUTABLE})
  if (DEFINED ENV{VIRTUAL_ENV} AND EXISTS "$ENV{VIRTUAL_ENV}/bin/python")
    set(Python_EXECUTABLE "$ENV{VIRTUAL_ENV}/bin/python" CACHE FILEPATH "Python interpreter")
    message(STATUS "Using Python from active virtualenv: ${Python_EXECUTABLE}")
  elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../.venv/bin/python")
    set(Python_EXECUTABLE "${CMAKE_CURRENT_SOURCE_DIR}/../.venv/bin/python" CACHE FILEPATH "Python interpreter")
    message(STATUS "Using Python from repo virtualenv: ${Python_EXECUTABLE}")
  endif()
endif()

find_package(Python 3.8 COMPONENTS Interpreter ${DEV_MODULE} REQUIRED)

# Detect the installed nanobind package
execute_process(
  COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
  OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE nanobind_ROOT)
find_package(nanobind CONFIG REQUIRED)

# ==============================================================================
# Discover and setup hardware-specific optimizations
# ==============================================================================

# Setup processor count for parallel operations
include(ProcessorCount)
ProcessorCount(CPU_CORES)
if(NOT CPU_CORES EQUAL 0)
  message(STATUS "Detected ${CPU_CORES} CPU cores for parallel operations")
else()
  set(CPU_CORES 1)
  message(STATUS "Could not detect CPU core count, defaulting to 1")
endif()

# Configure optimization flags based on compiler and build type
if (CMAKE_BUILD_TYPE STREQUAL "Release")
  if (CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # GCC or Clang optimizations with parallel LTO
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Ofast -march=native -flto=${CPU_CORES} -fno-math-errno")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto=${CPU_CORES}")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -flto=${CPU_CORES}")
    add_compile_options(-flto-partition=max -fuse-linker-plugin)
    # Configure environment variable for GCC LTO backend parallelism
    set(ENV{LTOJOBS} "${CPU_CORES}")
  elseif(MSVC)
    # MSVC optimizations
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG")
  endif()
endif()

# ==============================================================================
# Configure dependencies
# ==============================================================================

# Helper macro for finding a library via pkg-config or manual search
macro(find_and_add_library LIB_NAME PKG_NAME ERROR_MSG)
  find_package(PkgConfig QUIET)
  if (PkgConfig_FOUND)
    pkg_check_modules(${LIB_NAME} IMPORTED_TARGET ${PKG_NAME})
  endif()

  if (NOT ${LIB_NAME}_FOUND)
    find_path(${LIB_NAME}_INCLUDE_DIR NAMES ${ARGN})
    find_library(${LIB_NAME}_LIBRARY NAMES ${LIB_NAME})
    if (${LIB_NAME}_INCLUDE_DIR AND ${LIB_NAME}_LIBRARY)
      set(${LIB_NAME}_FOUND TRUE)
      add_library(${LIB_NAME}::${LIB_NAME} UNKNOWN IMPORTED)
      set_target_properties(${LIB_NAME}::${LIB_NAME} PROPERTIES
        IMPORTED_LOCATION "${${LIB_NAME}_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${${LIB_NAME}_INCLUDE_DIR}")
    endif()
  endif()

  if (NOT ${LIB_NAME}_FOUND)
    message(FATAL_ERROR "${ERROR_MSG}")
  endif()
endmacro()

# Find required libraries
find_and_add_library(ZSTD libzstd 
  "Could not find zstd library, required for bagz implementation" 
  zstd.h)

find_and_add_library(LZ4 liblz4 
  "Could not find lz4 library, required for .pgn.lz4 handling" 
  lz4.h lz4frame.h)

find_and_add_library(XXHASH libxxhash 
  "Could not find xxhash library, required for bucket shuffling" 
  xxhash.h)

find_and_add_library(FMT fmt 
  "Could not find fmt library, required for formatted text output" 
  fmt/format.h)

find_package(ZLIB REQUIRED)

# Find jemalloc library
find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
  pkg_check_modules(JEMALLOC IMPORTED_TARGET libjemalloc)
endif()

if (NOT JEMALLOC_FOUND)
  find_path(JEMALLOC_INCLUDE_DIR NAMES jemalloc/jemalloc.h)
  find_library(JEMALLOC_LIBRARY NAMES jemalloc)
  if (JEMALLOC_INCLUDE_DIR AND JEMALLOC_LIBRARY)
    set(JEMALLOC_FOUND TRUE)
    add_library(JEMALLOC::JEMALLOC UNKNOWN IMPORTED)
    set_target_properties(JEMALLOC::JEMALLOC PROPERTIES
      IMPORTED_LOCATION "${JEMALLOC_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${JEMALLOC_INCLUDE_DIR}")
  endif()
endif()

if (NOT JEMALLOC_FOUND)
  message(WARNING "Could not find jemalloc library, system allocator will be used instead")
else()
  message(STATUS "Found jemalloc: ${JEMALLOC_LIBRARY}")
endif()

# Setup JSON library (simdjson)
include(FetchContent)
FetchContent_Declare(
  simdjson
  URL https://github.com/simdjson/simdjson/archive/refs/tags/v3.12.3.tar.gz
)
# Ensure simdjson is built with PIC for shared library compatibility
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Disable the build static lib option to avoid warning
set(SIMDJSON_BUILD_STATIC_LIB OFF CACHE BOOL "Build simdjson static library" FORCE)
FetchContent_MakeAvailable(simdjson)

# Setup Quill logging library
FetchContent_Declare(
  quill
  URL https://github.com/odygrd/quill/archive/refs/tags/v7.5.0.tar.gz
)
set(QUILL_FMT_EXTERNAL ON CACHE BOOL "Use external fmt library" FORCE)
set(QUILL_BUILD_EXAMPLES OFF CACHE BOOL "Build Quill examples" FORCE)
set(QUILL_BUILD_TESTS OFF CACHE BOOL "Build Quill tests" FORCE)
set(QUILL_BUILD_BENCHMARKS OFF CACHE BOOL "Build Quill benchmarks" FORCE)
FetchContent_MakeAvailable(quill)

# ==============================================================================
# Setup GoogleTest for unit testing
# ==============================================================================

# Download and unpack googletest
configure_file(CMakeLists_GoogleTest.txt.in googletest-download/CMakeLists.txt)
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
        RESULT_VARIABLE result
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download)
if (result)
    message(FATAL_ERROR "CMake step for googletest failed: ${result}")
endif ()
execute_process(COMMAND ${CMAKE_COMMAND} --build .
        RESULT_VARIABLE result
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download)
if (result)
    message(FATAL_ERROR "Build step for googletest failed: ${result}")
endif ()

# Add googletest to the build and setup includes
add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/googletest-src
        ${CMAKE_CURRENT_BINARY_DIR}/googletest-build
        EXCLUDE_FROM_ALL)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/googletest-src/googletest/include)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/googletest-src/googlemock/include)

# ==============================================================================
# Setup Abseil for high-performance containers
# ==============================================================================

# Download and unpack abseil
configure_file(CMakeLists_Abseil.txt.in abseil-download/CMakeLists.txt)
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
        RESULT_VARIABLE result
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/abseil-download)
if (result)
    message(FATAL_ERROR "CMake step for abseil failed: ${result}")
endif ()
execute_process(COMMAND ${CMAKE_COMMAND} --build .
        RESULT_VARIABLE result
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/abseil-download)
if (result)
    message(FATAL_ERROR "Build step for abseil failed: ${result}")
endif ()

# Add abseil to the build with PIC enabled
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(ABSL_PROPAGATE_CXX_STD ON)
add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/abseil-src
        ${CMAKE_CURRENT_BINARY_DIR}/abseil-build
        EXCLUDE_FROM_ALL)

# ==============================================================================
# Helper function for linking common libraries
# ==============================================================================

# Function to link common dependencies to a target
# USE_JEMALLOC parameter controls whether to link jemalloc (default: ON)
function(link_common_dependencies TARGET)
  # Parse optional arguments
  set(options "")
  set(oneValueArgs USE_JEMALLOC)
  set(multiValueArgs "")
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  # Default to using jemalloc if not specified
  if(NOT DEFINED ARG_USE_JEMALLOC)
    set(ARG_USE_JEMALLOC ON)
  endif()

  target_include_directories(${TARGET} PRIVATE cpp_src)

  # Link standard libraries with appropriate fallbacks
  if (PkgConfig_FOUND AND ZSTD_FOUND)
    target_link_libraries(${TARGET} PRIVATE PkgConfig::ZSTD)
  else()
    target_link_libraries(${TARGET} PRIVATE ZSTD::ZSTD)
  endif()

  if (PkgConfig_FOUND AND LZ4_FOUND)
    target_link_libraries(${TARGET} PRIVATE PkgConfig::LZ4)
  else()
    target_link_libraries(${TARGET} PRIVATE LZ4::LZ4)
  endif()

  if (PkgConfig_FOUND AND XXHASH_FOUND)
    target_link_libraries(${TARGET} PRIVATE PkgConfig::XXHASH)
  else()
    target_link_libraries(${TARGET} PRIVATE XXHASH::XXHASH)
  endif()

  if (PkgConfig_FOUND AND FMT_FOUND)
    target_link_libraries(${TARGET} PRIVATE PkgConfig::FMT)
  else()
    target_link_libraries(${TARGET} PRIVATE FMT::FMT)
  endif()

  # Link jemalloc if found and requested
  if (JEMALLOC_FOUND AND ARG_USE_JEMALLOC)
    # Link with the library directly
    target_link_libraries(${TARGET} PRIVATE ${JEMALLOC_LIBRARY})

    # Define that jemalloc is available
    target_compile_definitions(${TARGET} PRIVATE HAVE_JEMALLOC)
  endif()

  target_link_libraries(${TARGET} PRIVATE simdjson::simdjson)
  target_link_libraries(${TARGET} PRIVATE ZLIB::ZLIB)
  target_link_libraries(${TARGET} PRIVATE quill::quill)
  target_link_libraries(${TARGET} PRIVATE 
    absl::flat_hash_map
    absl::flat_hash_set
    absl::strings
    absl::str_format)

  # Add compiler warnings/errors
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Werror)
  endif()
  
  # Define NDEBUG for Release and RelWithDebInfo builds to optimize simdjson performance
  target_compile_definitions(${TARGET} PRIVATE $<$<CONFIG:Release>:NDEBUG> $<$<CONFIG:RelWithDebInfo>:NDEBUG>)
endfunction()

# ==============================================================================
# Define source file sets for reuse
# ==============================================================================

# Core library files
set(BAGZ_CORE_SOURCES
    cpp_src/core/bagz.cpp
    cpp_src/core/bagz_mmap.cpp
    cpp_src/core/bagz_mmap_shared.cpp
    cpp_src/core/bagz_record_writer.cpp
    cpp_src/core/bagz_utils.cpp
)

# Utility source files
set(UTILS_SOURCES
    cpp_src/core/data_converter_base.cpp
    cpp_src/utils/file_utils.cpp
    cpp_src/pgn/pgn_parsing_utils.cpp
    cpp_src/io/lz4_pgn_stream_reader.cpp
    cpp_src/io/lz4_stream_reader.cpp
)

# Shuffle module sources (bucket-based shuffling infrastructure)
set(SHUFFLE_SOURCES
    cpp_src/shuffle/bucket_manager.cpp
)

# PGN converter module sources
set(PGN_CONVERTER_SOURCES
    cpp_src/pgn_converter/chess_pgn_visitor.cpp
    cpp_src/pgn_converter/pgn_to_bagz_converter.cpp
    cpp_src/pgn_converter/record_processor.cpp
    cpp_src/pgn_converter/external_sorter.cpp
    cpp_src/pgn_converter/pgn_processor.cpp
    cpp_src/pgn_converter/shuffle_manager.cpp
)

# Clock converter module sources
set(CLOCK_CONVERTER_SOURCES
    cpp_src/clock_converter/clock_position_record.cpp
    cpp_src/clock_converter/clock_data_converter.cpp
    cpp_src/clock_converter/clock_game_parser.cpp
    cpp_src/clock_converter/chunk_processor.cpp
    cpp_src/clock_converter/sorted_merge_writer.cpp
    cpp_src/clock_converter/common_position_writer.cpp
    cpp_src/clock_converter/common_position_loader.cpp
    cpp_src/clock_converter/gzip_utils.cpp
)

# Winner converter module sources
set(WINNER_CONVERTER_SOURCES
    cpp_src/winner_converter/winner_position_record.cpp
    cpp_src/winner_converter/winner_game_parser.cpp
    cpp_src/winner_converter/winner_data_converter.cpp
)

# Test-specific source files
set(TEST_SPECIFIC_SOURCES
    cpp_src/tests/run_tests.cpp
    cpp_src/tests/bagz_roundtrip_test.cpp
    cpp_src/tests/bagz_mmap_test.cpp
    cpp_src/tests/bagz_real_file_test.cpp
    cpp_src/tests/bagz_error_handling_test.cpp
    cpp_src/tests/python_interop_test.cpp
    cpp_src/shuffle/tests/bucket_record_test.cpp
    cpp_src/shuffle/tests/bucket_distribution_test.cpp
    cpp_src/shuffle/tests/bucket_manager_test.cpp
    cpp_src/shuffle/tests/bucket_manager_new_test.cpp
    cpp_src/shuffle/tests/bucket_overflow_test.cpp
    cpp_src/shuffle/tests/bucket_stats_race_test.cpp
    cpp_src/tests/memory_mapped_file_test.cpp
    cpp_src/tests/record_processor_test.cpp
    cpp_src/tests/external_sorter_test.cpp
    cpp_src/tests/pgn_processor_test.cpp
    cpp_src/tests/common_moves_test.cpp
    cpp_src/tests/thread_pool_test.cpp
    cpp_src/tests/common_position_extractor_test.cpp
    cpp_src/tests/common_position_integration_test.cpp
    cpp_src/tests/clock_position_record_test.cpp
    cpp_src/tests/clock_data_converter_test.cpp
    cpp_src/tests/clock_game_parser_test.cpp
    cpp_src/tests/clock_game_parser_before_move_test.cpp
    cpp_src/tests/clock_game_parser_new_behavior_test.cpp
    cpp_src/tests/bagz_record_writer_test.cpp
    cpp_src/tests/chunk_processor_test.cpp
    cpp_src/tests/lz4_stream_reader_test.cpp
    cpp_src/tests/sorted_merge_writer_test.cpp
    cpp_src/tests/common_position_writer_test.cpp
    cpp_src/tests/clock_converter_shuffle_test.cpp
    cpp_src/tests/shuffle_distribution_test.cpp
    cpp_src/tests/winner_position_record_test.cpp
    cpp_src/tests/winner_game_parser_test.cpp
    cpp_src/tests/winner_data_converter_test.cpp
    cpp_src/tests/winner_converter_integration_test.cpp
    cpp_src/tests/winner_converter_filter_test.cpp
        cpp_src/tests/winner_shuffle_test.cpp
    cpp_src/tests/memory_ordering_simple_test.cpp
)

# ==============================================================================
# Define targets and build executables
# ==============================================================================

# 1. Python extension module
set(PYTHON_MODULE_SOURCES
    cpp_src/bindings/chessmimic_core.cpp
    ${BAGZ_CORE_SOURCES}
    ${UTILS_SOURCES}
)
nanobind_add_module(chessmimic_core ${PYTHON_MODULE_SOURCES})
link_common_dependencies(chessmimic_core USE_JEMALLOC OFF)

# Generate type stubs for the chessmimic_core module
nanobind_add_stub(
  chessmimic_core_stub
  MODULE chessmimic_core
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/chessmimic_core/__init__.pyi
  PYTHON_PATH $<TARGET_FILE_DIR:chessmimic_core>
  DEPENDS chessmimic_core
  MARKER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/chessmimic_core/py.typed
  PATTERN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/chessmimic_core_pattern.txt
)

# 2. Main executable
set(PGN_TO_BAGZ_SOURCES
    cpp_src/apps/pgn_to_bagz_main.cpp
    ${UTILS_SOURCES}
    ${BAGZ_CORE_SOURCES}
    ${SHUFFLE_SOURCES}
    ${PGN_CONVERTER_SOURCES}
)
add_executable(pgn_to_bagz ${PGN_TO_BAGZ_SOURCES})
link_common_dependencies(pgn_to_bagz USE_JEMALLOC ON)

# Make sure jemalloc is loaded first for pgn_to_bagz
if (JEMALLOC_FOUND)
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(pgn_to_bagz PRIVATE -Wl,--no-as-needed)
  endif()
endif()

# Common position sources
set(COMMON_POSITION_SOURCES
    cpp_src/common_position/common_position_extractor.cpp
    cpp_src/common_position/position_data.cpp
    cpp_src/common_position/bagz_reader_impl.cpp
    cpp_src/common_position/file_operations_impl.cpp
)

# 3. Extract common positions executable
set(EXTRACT_COMMON_POSITIONS_SOURCES
    cpp_src/apps/extract_common_positions_main.cpp
    ${COMMON_POSITION_SOURCES}
    ${UTILS_SOURCES}
    ${BAGZ_CORE_SOURCES}
        cpp_src/common_position/common_position.hpp
)
add_executable(extract_common_positions ${EXTRACT_COMMON_POSITIONS_SOURCES})
link_common_dependencies(extract_common_positions USE_JEMALLOC ON)

# Make sure jemalloc is loaded first for extract_common_positions
if (JEMALLOC_FOUND)
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(extract_common_positions PRIVATE -Wl,--no-as-needed)
  endif()
endif()

# 4. Clock BAGZ converter executable
set(PGN_TO_CLOCK_BAGZ_SOURCES
    cpp_src/apps/pgn_to_clock_bagz_main.cpp
    ${CLOCK_CONVERTER_SOURCES}
    ${UTILS_SOURCES}
    ${BAGZ_CORE_SOURCES}
    ${SHUFFLE_SOURCES}
    cpp_src/pgn_converter/shuffle_manager.cpp
    cpp_src/pgn_converter/record_processor.cpp
)
add_executable(pgn_to_clock_bagz ${PGN_TO_CLOCK_BAGZ_SOURCES})
link_common_dependencies(pgn_to_clock_bagz USE_JEMALLOC ON)

# Make sure jemalloc is loaded first for pgn_to_clock_bagz
if (JEMALLOC_FOUND)
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(pgn_to_clock_bagz PRIVATE -Wl,--no-as-needed)
  endif()
endif()

# 5. Winner BAGZ converter executable
set(PGN_TO_WINNER_BAGZ_SOURCES
    cpp_src/apps/pgn_to_winner_bagz_main.cpp
    ${WINNER_CONVERTER_SOURCES}
    ${UTILS_SOURCES}
    ${BAGZ_CORE_SOURCES}
    ${SHUFFLE_SOURCES}
    cpp_src/pgn_converter/shuffle_manager.cpp
    cpp_src/pgn_converter/record_processor.cpp  # Needed by ShuffleManager
)
add_executable(pgn_to_winner_bagz ${PGN_TO_WINNER_BAGZ_SOURCES})
link_common_dependencies(pgn_to_winner_bagz USE_JEMALLOC ON)

# Make sure jemalloc is loaded first for pgn_to_winner_bagz
if (JEMALLOC_FOUND)
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(pgn_to_winner_bagz PRIVATE -Wl,--no-as-needed)
  endif()
endif()

# 6. Test executable
set(TEST_SOURCES
    ${TEST_SPECIFIC_SOURCES}
    ${COMMON_POSITION_SOURCES}
    ${UTILS_SOURCES}
    ${BAGZ_CORE_SOURCES}
    ${SHUFFLE_SOURCES}
    ${PGN_CONVERTER_SOURCES}
    ${CLOCK_CONVERTER_SOURCES}
    ${WINNER_CONVERTER_SOURCES}
)
add_executable(run_tests ${TEST_SOURCES})
link_common_dependencies(run_tests USE_JEMALLOC ON)
target_link_libraries(run_tests PRIVATE gtest gtest_main gmock)

# Configure CTest
enable_testing()
include(GoogleTest)
gtest_discover_tests(run_tests)

# ==============================================================================
# Installation
# ==============================================================================

install(TARGETS chessmimic_core DESTINATION .)
install(TARGETS pgn_to_bagz DESTINATION bin)
install(TARGETS extract_common_positions DESTINATION bin)
install(TARGETS pgn_to_clock_bagz DESTINATION bin)
install(TARGETS pgn_to_winner_bagz DESTINATION bin)

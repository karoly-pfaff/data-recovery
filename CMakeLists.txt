# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.25)

project(revenant
    VERSION 0.4.0
    DESCRIPTION "Precise, structure-aware data-recovery toolkit"
    HOMEPAGE_URL "https://example.invalid/revenant"
    LANGUAGES CXX
)

# --- Global standard: C++20, no compiler extensions ---------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clang-tidy / clang-format / editors.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- Options ------------------------------------------------------------------
option(REVENANT_BUILD_TESTS "Build the test suite" ON)
option(REVENANT_BUILD_FUZZERS "Build libFuzzer targets (Clang only)" OFF)
option(REVENANT_ENABLE_SANITIZERS "Enable ASan/UBSan (and TSan on Linux)" OFF)
option(REVENANT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(REVENANT_ENABLE_COVERAGE "Instrument for coverage" OFF)

include(CompilerWarnings)
include(Sanitizers)

# What a benchmark harness needs to know about a build it did not perform. The
# numbers off a debug or sanitized binary look exactly like real ones and mean
# nothing, and the build is the only honest witness to which it was — so it
# says so, beside the binaries, where a CI artifact carries it along with them.
if(REVENANT_ENABLE_SANITIZERS)
    set(revenant_sanitized_json "true")
else()
    set(revenant_sanitized_json "false")
endif()
file(WRITE "${CMAKE_BINARY_DIR}/build-info.json"
    "{\n"
    "  \"build_type\": \"${CMAKE_BUILD_TYPE}\",\n"
    "  \"sanitizers\": ${revenant_sanitized_json}\n"
    "}\n")

if(MSVC AND REVENANT_ENABLE_SANITIZERS)
    # CMake's default Debug /RTC1 is incompatible with /fsanitize=address (D8016).
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

# An INTERFACE target that carries the project-wide compile contract. Every
# Revenant target links this to inherit warnings and sanitizer flags.
add_library(revenant_project_options INTERFACE)
target_compile_features(revenant_project_options INTERFACE cxx_std_20)
revenant_set_project_warnings(revenant_project_options)
revenant_set_sanitizers(revenant_project_options)

if(REVENANT_ENABLE_COVERAGE)
    target_compile_options(revenant_project_options INTERFACE
        $<$<CXX_COMPILER_ID:GNU>:--coverage>
        $<$<CXX_COMPILER_ID:Clang>:-fprofile-instr-generate -fcoverage-mapping>)
    target_link_options(revenant_project_options INTERFACE
        $<$<CXX_COMPILER_ID:GNU>:--coverage>
        $<$<CXX_COMPILER_ID:Clang>:-fprofile-instr-generate -fcoverage-mapping>)
endif()

# A fuzz target's `-fsanitize=fuzzer` instruments the harness translation unit
# and nothing else, so without this the library holding every parser is compiled
# with no SanitizerCoverage at all and libFuzzer mutates blind: it can see its
# own harness and not the code under test. `fuzzer-no-link` is the half that
# instruments without claiming `main`, which is what a library wants (story-0606).
if(REVENANT_BUILD_FUZZERS)
    target_compile_options(revenant_project_options INTERFACE
        $<$<CXX_COMPILER_ID:Clang>:-fsanitize=fuzzer-no-link>)
    target_link_options(revenant_project_options INTERFACE
        $<$<CXX_COMPILER_ID:Clang>:-fsanitize=fuzzer-no-link>)
endif()

# --- Source & tests (filled in from milestone M0 onward) ----------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/CMakeLists.txt")
    add_subdirectory(src)
endif()

if(REVENANT_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()

option(REVENANT_BUILD_TOOLS "Build developer tools (image generators)" ON)
if(REVENANT_BUILD_TOOLS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/imagegen/CMakeLists.txt")
    add_subdirectory(tools/imagegen)
endif()

# --- Developer convenience targets (format / tidy / guards) -------------------
include(DevTargets)
revenant_add_dev_targets()

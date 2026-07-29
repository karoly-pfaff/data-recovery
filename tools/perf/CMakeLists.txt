# SPDX-License-Identifier: GPL-3.0-or-later
# The benchmark suite, split the way it is tested: the harness is a library the
# test binary links, and the benchmark *cases* belong to the executable.
#
# The split is not cosmetic. The cases compile tests/support/InMemoryDevice.cpp
# — the same privilege-free byte source the fuzz targets pull in — and the test
# binary compiles it too, so a library carrying both would collide at link.
#
# Built with the tests rather than only in the `release` preset, so it stays
# compiling, formatted and tidied like the rest of the tree. Whether its numbers
# mean anything is a separate question, and `revenant-bench` answers it itself: a
# build without NDEBUG says so before printing one.

add_library(revenant_perf STATIC
    Benchmark.cpp
    BenchMain.cpp
    Report.cpp
    Statistics.cpp
)
target_include_directories(revenant_perf PUBLIC ${CMAKE_SOURCE_DIR}/tools)
target_link_libraries(revenant_perf PRIVATE revenant_project_options)

add_executable(revenant-bench
    Main.cpp
    BenchmarkInput.cpp
    Benchmarks.cpp
    ${CMAKE_SOURCE_DIR}/tests/support/InMemoryDevice.cpp
)
target_include_directories(revenant-bench PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(revenant-bench
    PRIVATE revenant_perf revenant revenant_imagegen revenant_project_options)

# SPDX-License-Identifier: GPL-3.0-or-later
find_package(GTest CONFIG REQUIRED)

add_executable(revenant_tests
    support/InMemoryDevice.cpp
    support/TempFile.cpp
    integration/ImageFileDeviceTest.cpp
    integration/ImagegenRoundtripTest.cpp
    integration/JpegCarveGoldenTest.cpp
    unit/carve/CarverRegistryTest.cpp
    unit/carve/SignatureScannerTest.cpp
    unit/carve/formats/JpegCarverTest.cpp
    unit/core/BoundedCountTest.cpp
    unit/core/ByteReaderTest.cpp
    unit/core/EndianTest.cpp
    unit/core/EndianWriteTest.cpp
    unit/core/LoggerTest.cpp
    unit/core/ResultTest.cpp
    unit/core/VersionTest.cpp
    unit/fs/NameDecodeTest.cpp
    unit/io/InMemoryDeviceTest.cpp
    unit/io/ReadRangeTest.cpp
    unit/recovery/DisambiguateTest.cpp
    unit/recovery/OutputPathTest.cpp
    unit/tools/ImagegenCliTest.cpp
    unit/tools/PatternWriterTest.cpp
)
target_include_directories(revenant_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(revenant_tests PRIVATE
    revenant
    revenant_imagegen
    revenant_project_options
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(revenant_tests DISCOVERY_TIMEOUT 60)

if(REVENANT_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()

find_program(REVENANT_TEST_PYTHON NAMES python3 python)
if(REVENANT_TEST_PYTHON)
    add_test(NAME CoverageGatePassesAboveFloor
        COMMAND ${REVENANT_TEST_PYTHON} ${CMAKE_SOURCE_DIR}/tools/lint/check_coverage.py
                --min 85 --export ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/coverage/passing.json
                --root /repo --prefix src --prefix include)
    add_test(NAME CoverageGateFailsBelowFloor
        COMMAND ${REVENANT_TEST_PYTHON} ${CMAKE_SOURCE_DIR}/tools/lint/check_coverage.py
                --min 85 --export ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/coverage/failing.json
                --root /repo --prefix src --prefix include)
    set_tests_properties(CoverageGateFailsBelowFloor PROPERTIES
        PASS_REGULAR_EXPRESSION "core line coverage: 42\\.9% \\(floor 85\\.0%\\)")
    add_test(NAME CoverageGateRefusesEmptyMatch
        COMMAND ${REVENANT_TEST_PYTHON} ${CMAKE_SOURCE_DIR}/tools/lint/check_coverage.py
                --min 85 --export ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/coverage/passing.json
                --root /repo --prefix nonexistent)
    set_tests_properties(CoverageGateRefusesEmptyMatch PROPERTIES
        PASS_REGULAR_EXPRESSION "refusing to pass an empty gate")
endif()

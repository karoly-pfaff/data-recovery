# SPDX-License-Identifier: GPL-3.0-or-later
find_package(GTest CONFIG REQUIRED)

add_executable(revenant_tests
    unit/core/VersionTest.cpp
)
target_include_directories(revenant_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(revenant_tests PRIVATE
    revenant
    revenant_project_options
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(revenant_tests DISCOVERY_TIMEOUT 60)

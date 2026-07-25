# SPDX-License-Identifier: GPL-3.0-or-later
# Synthetic-image generator: logic in a static lib (unit-testable), thin exe.

add_library(revenant_imagegen STATIC
    PatternWriter.cpp
    CliMain.cpp
)
target_include_directories(revenant_imagegen PUBLIC ${CMAKE_SOURCE_DIR}/tools)
target_link_libraries(revenant_imagegen
    PUBLIC revenant
    PRIVATE revenant_project_options)

add_executable(revenant-imagegen Main.cpp)
target_link_libraries(revenant-imagegen PRIVATE revenant_imagegen revenant_project_options)

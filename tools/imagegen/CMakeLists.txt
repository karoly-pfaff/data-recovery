# SPDX-License-Identifier: GPL-3.0-or-later
# Synthetic-image generator: logic in a static lib (unit-testable), thin exe.

add_library(revenant_imagegen STATIC
    PatternWriter.cpp
    CliMain.cpp
    FixtureBytes.cpp
    exfat/ExfatEntries.cpp
    exfat/ExfatFixture.cpp
    exfat/ExfatImageBuilder.cpp
    exfat/ExfatLayout.cpp
    fat/Fat32Directories.cpp
    fat/Fat32Entries.cpp
    fat/Fat32Fixture.cpp
    fat/Fat32ImageBuilder.cpp
    fat/Fat32Layout.cpp
    ntfs/AttributeBuilder.cpp
    ntfs/BootSectorBuilder.cpp
    ntfs/FileAttributes.cpp
    ntfs/FixtureFiles.cpp
    ntfs/MftRecordBuilder.cpp
    ntfs/NtfsImageBuilder.cpp
    ntfs/NtfsLayout.cpp
    ntfs/RunlistEncoder.cpp
)
target_include_directories(revenant_imagegen PUBLIC ${CMAKE_SOURCE_DIR}/tools)
target_link_libraries(revenant_imagegen
    PUBLIC revenant
    PRIVATE revenant_project_options)

add_executable(revenant-imagegen Main.cpp)
target_link_libraries(revenant-imagegen PRIVATE revenant_imagegen revenant_project_options)

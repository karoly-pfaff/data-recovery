# SPDX-License-Identifier: GPL-3.0-or-later
find_package(GTest CONFIG REQUIRED)

add_executable(revenant_tests
    support/CliFixture.cpp
    support/FaultyDevice.cpp
    support/FixtureContent.cpp
    support/GptFixture.cpp
    support/InMemoryDevice.cpp
    support/MftRecordTestSupport.cpp
    support/NtfsVolume.cpp
    support/TempDir.cpp
    support/TempFile.cpp
    integration/ImageFileDeviceTest.cpp
    integration/ImagegenRoundtripTest.cpp
    integration/ArbitratedRecoveryTest.cpp
    integration/HybridRecoveryTest.cpp
    integration/JpegCarveGoldenTest.cpp
    integration/RecoveredFilesTest.cpp
    integration/ExfatEnumerationTest.cpp
    integration/Ext4EnumerationTest.cpp
    integration/Fat32EnumerationTest.cpp
    integration/NtfsEnumerationTest.cpp
    integration/CarveCliTest.cpp
    integration/ResumedRecoveryTest.cpp
    integration/UndeleteCliTest.cpp
    integration/NtfsImageTest.cpp
    integration/PartitionSelectionTest.cpp
    unit/cli/CarveOptionsTest.cpp
    unit/cli/InterruptTest.cpp
    unit/cli/PartitionListingTest.cpp
    unit/cli/RecoveryRunTest.cpp
    unit/cli/RunSummaryTest.cpp
    unit/cli/UndeleteOptionsTest.cpp
    unit/carve/CarverRegistryTest.cpp
    unit/carve/PlausibilityTest.cpp
    unit/carve/ScanRegionTest.cpp
    unit/carve/SignatureScannerTest.cpp
    unit/carve/formats/JpegCarverTest.cpp
    unit/carve/formats/Mp4CarverTest.cpp
    unit/carve/formats/PdfCarverTest.cpp
    unit/carve/formats/PngCarverTest.cpp
    unit/carve/formats/RawCarverTest.cpp
    unit/carve/formats/ZipCarverTest.cpp
    unit/core/BoundedCountTest.cpp
    unit/core/ByteReaderTest.cpp
    unit/core/Crc32Test.cpp
    unit/core/EndianTest.cpp
    unit/core/EndianWriteTest.cpp
    unit/core/LoggerTest.cpp
    unit/core/ResultTest.cpp
    unit/core/Sha256Test.cpp
    unit/core/VersionTest.cpp
    unit/fs/ClusterChainTest.cpp
    unit/fs/ExtentLocateTest.cpp
    unit/fs/ext4/BlockReaderTest.cpp
    unit/fs/ext4/DirectoryEntryTest.cpp
    unit/fs/ext4/DirectoryHoleTest.cpp
    unit/fs/ext4/ExtentTreeTest.cpp
    unit/fs/ext4/ExtentWalkTest.cpp
    unit/fs/ext4/GroupDescriptorTest.cpp
    unit/fs/ext4/InodeTest.cpp
    unit/fs/ext4/JournalTest.cpp
    unit/fs/ext4/MountTest.cpp
    unit/fs/ext4/OrphanListTest.cpp
    unit/fs/ext4/SuperblockTest.cpp
    unit/fs/exfat/BootRegionTest.cpp
    unit/fs/exfat/DirectoryEntryTest.cpp
    unit/fs/exfat/MountTest.cpp
    unit/fs/fat/BootSectorTest.cpp
    unit/fs/fat/DirectoryEntryTest.cpp
    unit/fs/fat/DosTimeTest.cpp
    unit/fs/fat/ShortNameTest.cpp
    unit/fs/MountTest.cpp
    unit/fs/NameDecodeTest.cpp
    unit/fs/RawNameTest.cpp
    unit/fs/UnixTimeTest.cpp
    unit/fs/ntfs/BootSectorTest.cpp
    unit/fs/ntfs/EntryEnumerationTest.cpp
    unit/fs/ntfs/EntryPathTest.cpp
    unit/fs/ntfs/MftRecordTest.cpp
    unit/fs/ntfs/MftTableTest.cpp
    unit/fs/ntfs/RunlistExtentsTest.cpp
    unit/fs/ntfs/RunlistTest.cpp
    unit/io/CachingDeviceTest.cpp
    unit/io/InMemoryDeviceTest.cpp
    unit/io/RetryingDeviceTest.cpp
    unit/io/ReadRangeTest.cpp
    unit/recovery/ArbitrationTest.cpp
    unit/recovery/ByteAccountingTest.cpp
    unit/recovery/CandidateIndexTest.cpp
    unit/recovery/CheckpointTest.cpp
    unit/recovery/DisambiguateTest.cpp
    unit/recovery/HybridRecoveryTest.cpp
    unit/recovery/ManifestTest.cpp
    unit/recovery/RegionSetTest.cpp
    unit/recovery/ScanRegionsTest.cpp
    unit/volume/GptPartitionsTest.cpp
    unit/volume/GptTest.cpp
    unit/volume/MbrPartitionsTest.cpp
    unit/volume/MbrTest.cpp
    unit/volume/PartitionLabelTest.cpp
    unit/volume/PartitionTableTest.cpp
    unit/volume/PartitionViewTest.cpp
    unit/recovery/OutputNameTest.cpp
    unit/recovery/OutputPathTest.cpp
    unit/recovery/RecoverySinkTest.cpp
    unit/tools/AttributeBuilderTest.cpp
    unit/tools/BootSectorBuilderTest.cpp
    unit/tools/ImagegenCliTest.cpp
    unit/tools/MftRecordBuilderTest.cpp
    unit/tools/NtfsLayoutTest.cpp
    unit/tools/PatternWriterTest.cpp
    unit/tools/RunlistEncoderTest.cpp
)
target_include_directories(revenant_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(revenant_tests PRIVATE
    revenant
    revenant_cli
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

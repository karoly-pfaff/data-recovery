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
    support/RecoveryPipeline.cpp
    support/ReferenceMatcher.cpp
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
    integration/SourceUnchangedTest.cpp
    integration/UndeleteCliTest.cpp
    integration/NtfsImageTest.cpp
    integration/PartitionSelectionTest.cpp
    integration/WholeDiskRecoveryTest.cpp
    unit/cli/CarveOptionsTest.cpp
    unit/cli/InterruptTest.cpp
    unit/cli/PartitionListingTest.cpp
    unit/cli/RecoveryRunTest.cpp
    unit/cli/RunSummaryTest.cpp
    unit/cli/UndeleteOptionsTest.cpp
    unit/carve/CarverRegistryTest.cpp
    unit/carve/MatcherDifferentialTest.cpp
    unit/carve/SignatureTableTest.cpp
    unit/carve/PlausibilityTest.cpp
    unit/carve/PrefilterTest.cpp
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
    unit/core/SafeArithTest.cpp
    unit/core/Sha256Test.cpp
    unit/core/Utf16NameTest.cpp
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
    unit/fs/RawNameTest.cpp
    unit/fs/UnixTimeTest.cpp
    unit/fs/ntfs/BootSectorTest.cpp
    unit/fs/ntfs/EntryEnumerationTest.cpp
    unit/fs/ntfs/EntryPathTest.cpp
    unit/fs/ntfs/MftRecordTest.cpp
    unit/fs/ntfs/MftTableTest.cpp
    unit/fs/ntfs/RunlistExtentsTest.cpp
    unit/fs/ntfs/RunlistTest.cpp
    unit/io/AlignedReadTest.cpp
    unit/io/CachingDeviceTest.cpp
    unit/io/DeviceIdentityTest.cpp
    unit/io/InMemoryDeviceTest.cpp
    unit/io/MountTableTest.cpp
    unit/io/SysfsWalkTest.cpp
    unit/io/RetryingDeviceTest.cpp
    unit/io/ReadRangeTest.cpp
    unit/io/SourceDeviceTest.cpp
    unit/recovery/ArbitrationTest.cpp
    unit/recovery/ByteAccountingTest.cpp
    unit/recovery/CandidateIndexTest.cpp
    unit/recovery/CheckpointTest.cpp
    unit/recovery/DestinationRuleTest.cpp
    unit/recovery/DisambiguateTest.cpp
    unit/recovery/HybridRecoveryTest.cpp
    unit/recovery/ManifestTest.cpp
    unit/recovery/PartitionedWalkTest.cpp
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
    unit/tools/CarveCorpusTest.cpp
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

    # The format gate's own refusal, end to end: a root that does not exist
    # must stop the gate naming it, not quietly shrink the covered set.
    add_test(NAME FormatGateRefusesAMissingRoot
        COMMAND ${REVENANT_TEST_PYTHON} ${CMAKE_SOURCE_DIR}/tools/lint/check_format.py
                ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/nonexistent)
    set_tests_properties(FormatGateRefusesAMissingRoot PROPERTIES
        PASS_REGULAR_EXPRESSION "gate root does not exist")

    # The performance gate, driven the same way: fixtures in, verdict out. The
    # benchmark *numbers* are machine-dependent and untestable; the ruling on
    # them is not, and it is the part that fails a merge.
    set(REVENANT_PERF_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/perf)
    set(REVENANT_PERF_GATE ${CMAKE_SOURCE_DIR}/tools/perf/compare_baseline.py)

    add_test(NAME PerfGatePassesAnUnchangedRun
        COMMAND ${REVENANT_TEST_PYTHON} ${REVENANT_PERF_GATE}
                --baseline ${REVENANT_PERF_FIXTURES}/baseline.json
                --current ${REVENANT_PERF_FIXTURES}/unchanged.json)

    add_test(NAME PerfGatePassesADropInsideTheThreshold
        COMMAND ${REVENANT_TEST_PYTHON} ${REVENANT_PERF_GATE}
                --baseline ${REVENANT_PERF_FIXTURES}/baseline.json
                --current ${REVENANT_PERF_FIXTURES}/within-threshold.json)

    # A runner whose repetitions already disagreed by 35% cannot tell a 30%
    # regression from its own noise.
    add_test(NAME PerfGatePassesADropInsideTheBaselinesOwnSpread
        COMMAND ${REVENANT_TEST_PYTHON} ${REVENANT_PERF_GATE}
                --baseline ${REVENANT_PERF_FIXTURES}/noisy-baseline.json
                --current ${REVENANT_PERF_FIXTURES}/noisy-current.json)

    # Peak memory and instruction count are properties of the program rather
    # than of the machine, so they are gated far tighter than a rate — and the
    # timings' spread does not excuse them.
    #
    # This first case is the calibration written down as a test: 5.3% is what
    # two consecutive CI runs of near-identical code differed by on the one
    # benchmark whose footprint is small enough for a single allocator arena to
    # be a noticeable fraction of it. A gate that called that a regression
    # would be red for reasons nobody could act on.
    add_test(NAME PerfGatePassesThePeakMemoryNoiseTwoRunnersProduce
        COMMAND ${REVENANT_TEST_PYTHON} ${REVENANT_PERF_GATE}
                --baseline ${REVENANT_PERF_FIXTURES}/baseline.json
                --current ${REVENANT_PERF_FIXTURES}/rss-within-threshold.json)

    add_test(NAME PerfGateFailsAPeakMemoryRegression
        COMMAND ${REVENANT_TEST_PYTHON} ${REVENANT_PERF_GATE}
                --baseline ${REVENANT_PERF_FIXTURES}/baseline.json
                --current ${REVENANT_PERF_FIXTURES}/rss-regressed.json)
    set_tests_properties(PerfGateFailsAPeakMemoryRegression PROPERTIES WILL_FAIL TRUE)

    add_test(NAME PerfGateFailsAnInstructionCountRegression
        COMMAND ${REVENANT_TEST_PYTHON} ${REVENANT_PERF_GATE}
                --baseline ${REVENANT_PERF_FIXTURES}/baseline.json
                --current ${REVENANT_PERF_FIXTURES}/instructions-regressed.json)
    set_tests_properties(PerfGateFailsAnInstructionCountRegression PROPERTIES WILL_FAIL TRUE)

    add_test(NAME PerfGateAcceptsANewBenchmarkWithoutGatingIt
        COMMAND ${REVENANT_TEST_PYTHON} ${REVENANT_PERF_GATE}
                --baseline ${REVENANT_PERF_FIXTURES}/baseline.json
                --current ${REVENANT_PERF_FIXTURES}/added.json)
    set_tests_properties(PerfGateAcceptsANewBenchmarkWithoutGatingIt PROPERTIES
        PASS_REGULAR_EXPRESSION "new benchmark.*end-to-end-hybrid")

    # A real regression does more work, and the instruction count says so. The
    # rate complaint has to be *in the output*, not merely implied by the exit
    # code, because the instruction complaint would fail this run on its own.
    add_test(NAME PerfGateFailsARealRegressionAndNamesIt
        COMMAND ${REVENANT_TEST_PYTHON} ${REVENANT_PERF_GATE}
                --baseline ${REVENANT_PERF_FIXTURES}/baseline.json
                --current ${REVENANT_PERF_FIXTURES}/regressed.json)
    set_tests_properties(PerfGateFailsARealRegressionAndNamesIt PROPERTIES
        PASS_REGULAR_EXPRESSION "regression: scan-throughput: 30\.0% slower")

    # And the observation that made the corroboration rule necessary: on
    # 2026-07-30 a case ran 6.9x slower on a runner while executing 16% *fewer*
    # instructions than the baseline, twice in a row, with a 0.4% within-run
    # spread. A rate alone cannot tell a slow host from slow code; the
    # instruction count can, and here it acquits the change.
    add_test(NAME PerfGatePassesARateDropTheInstructionCountContradicts
        COMMAND ${REVENANT_TEST_PYTHON} ${REVENANT_PERF_GATE}
                --baseline ${REVENANT_PERF_FIXTURES}/baseline.json
                --current ${REVENANT_PERF_FIXTURES}/rate-drop-uncorroborated.json)
    set_tests_properties(PerfGatePassesARateDropTheInstructionCountContradicts PROPERTIES
        PASS_REGULAR_EXPRESSION "is the machine rather than the change")

    # A gate that silently stops measuring something is a fake gate.
    add_test(NAME PerfGateFailsWhenABenchmarkDisappears
        COMMAND ${REVENANT_TEST_PYTHON} ${REVENANT_PERF_GATE}
                --baseline ${REVENANT_PERF_FIXTURES}/baseline.json
                --current ${REVENANT_PERF_FIXTURES}/missing.json)
    set_tests_properties(PerfGateFailsWhenABenchmarkDisappears PROPERTIES
        WILL_FAIL TRUE)

    # The duplication gate's refusal, end to end, on the same terms as the
    # format gate's: a root that does not exist stops the gate naming it.
    # The threshold here is deliberately not the gate's: nothing is scanned, so
    # any value would do, and the one that governs a merge is owned elsewhere.
    add_test(NAME DuplicationGateRefusesAMissingRoot
        COMMAND ${REVENANT_TEST_PYTHON} ${CMAKE_SOURCE_DIR}/tools/lint/check_duplication.py
                --min-tokens 1 ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/nonexistent)
    set_tests_properties(DuplicationGateRefusesAMissingRoot PROPERTIES
        PASS_REGULAR_EXPRESSION "gate root does not exist")

    # The gate scripts' own units, whatever `tests/unit/lint/` holds — the list
    # is the directory, and naming it here again is how it goes stale.
    add_test(NAME LintUnitTests
        COMMAND ${REVENANT_TEST_PYTHON} -m unittest discover
                --start-directory ${CMAKE_CURRENT_SOURCE_DIR}/unit/lint --verbose)
endif()

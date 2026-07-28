# SPDX-License-Identifier: GPL-3.0-or-later
# libFuzzer targets — Clang-only, built via the `fuzz` preset.

function(revenant_add_fuzz_target name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE revenant revenant_project_options)
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer)
endfunction()

revenant_add_fuzz_target(ByteReaderFuzz ByteReaderFuzz.cpp)

revenant_add_fuzz_target(SignatureScanFuzz SignatureScanFuzz.cpp)
target_sources(SignatureScanFuzz PRIVATE ${CMAKE_SOURCE_DIR}/tests/support/InMemoryDevice.cpp)
target_include_directories(SignatureScanFuzz PRIVATE ${CMAKE_SOURCE_DIR}/tests)

revenant_add_fuzz_target(JpegCarverFuzz JpegCarverFuzz.cpp)
target_include_directories(JpegCarverFuzz PRIVATE ${CMAKE_SOURCE_DIR}/src)

revenant_add_fuzz_target(Mp4CarverFuzz Mp4CarverFuzz.cpp)
target_include_directories(Mp4CarverFuzz PRIVATE ${CMAKE_SOURCE_DIR}/src)

revenant_add_fuzz_target(RawCarverFuzz RawCarverFuzz.cpp)
target_include_directories(RawCarverFuzz PRIVATE ${CMAKE_SOURCE_DIR}/src)

revenant_add_fuzz_target(ZipCarverFuzz ZipCarverFuzz.cpp)
target_include_directories(ZipCarverFuzz PRIVATE ${CMAKE_SOURCE_DIR}/src)

revenant_add_fuzz_target(PdfCarverFuzz PdfCarverFuzz.cpp)
target_include_directories(PdfCarverFuzz PRIVATE ${CMAKE_SOURCE_DIR}/src)

revenant_add_fuzz_target(PngCarverFuzz PngCarverFuzz.cpp)
target_include_directories(PngCarverFuzz PRIVATE ${CMAKE_SOURCE_DIR}/src)

revenant_add_fuzz_target(OutputPathFuzz OutputPathFuzz.cpp)

revenant_add_fuzz_target(NameDecodeFuzz NameDecodeFuzz.cpp)

revenant_add_fuzz_target(NtfsBootSectorFuzz NtfsBootSectorFuzz.cpp)

revenant_add_fuzz_target(MftRecordFuzz MftRecordFuzz.cpp)

revenant_add_fuzz_target(RunlistFuzz RunlistFuzz.cpp)

revenant_add_fuzz_target(NtfsEnumerateFuzz NtfsEnumerateFuzz.cpp)
target_sources(NtfsEnumerateFuzz PRIVATE ${CMAKE_SOURCE_DIR}/tests/support/InMemoryDevice.cpp)
target_include_directories(NtfsEnumerateFuzz PRIVATE ${CMAKE_SOURCE_DIR}/tests)

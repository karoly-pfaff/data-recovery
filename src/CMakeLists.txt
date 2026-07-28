# SPDX-License-Identifier: GPL-3.0-or-later
# The shared static core. Frontends and tests link `revenant`; nothing links
# platform code directly (DIP: consumers see interfaces in include/revenant).

add_library(revenant STATIC
    carve/BuiltinCarvers.cpp
    carve/CarverRegistry.cpp
    carve/SignatureScanner.cpp
    carve/WindowMatch.cpp
    carve/formats/AsciiText.cpp
    carve/formats/JpegCarver.cpp
    carve/formats/JpegEntropyScan.cpp
    carve/formats/JpegMarkerWalk.cpp
    carve/formats/Mp4BoxWalk.cpp
    carve/formats/Mp4Carver.cpp
    carve/formats/PdfCarver.cpp
    carve/formats/PdfTrailer.cpp
    carve/formats/PngCarver.cpp
    carve/formats/PngChunkWalk.cpp
    carve/formats/RawCarver.cpp
    carve/formats/TiffEntry.cpp
    carve/formats/TiffIfdWalk.cpp
    carve/formats/ZipCarver.cpp
    carve/formats/ZipEndRecord.cpp
    core/ByteReader.cpp
    core/Crc32.cpp
    core/Version.cpp
    core/io/ImageFileDeviceShared.cpp
    core/log/LogLevel.cpp
    core/log/Logger.cpp
    core/log/StderrSink.cpp
    fs/NameDecode.cpp
    fs/ntfs/BootSector.cpp
    fs/ntfs/BootSectorFields.cpp
    fs/ntfs/MftAttributes.cpp
    fs/ntfs/MftRecord.cpp
    fs/ntfs/MftRecordAttributes.cpp
    fs/ntfs/MftRecordFixup.cpp
    fs/ntfs/Runlist.cpp
    fs/ntfs/RunlistExtents.cpp
    fs/ntfs/RunlistRun.cpp
    volume/PartitionView.cpp
    recovery/Disambiguate.cpp
    recovery/OutputPath.cpp
    recovery/OutputPathSegment.cpp
)
add_library(revenant::revenant ALIAS revenant)

if(WIN32)
    target_sources(revenant PRIVATE core/io/ImageFileDeviceWindows.cpp)
    set_source_files_properties(core/io/ImageFileDeviceWindows.cpp PROPERTIES
        COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN;NOMINMAX")
else()
    target_sources(revenant PRIVATE core/io/ImageFileDevicePosix.cpp)
endif()

target_include_directories(revenant PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(revenant PRIVATE REVENANT_VERSION="${PROJECT_VERSION}")
target_link_libraries(revenant PRIVATE revenant_project_options)

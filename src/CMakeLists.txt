# SPDX-License-Identifier: GPL-3.0-or-later
# The shared static core. Frontends and tests link `revenant`; nothing links
# platform code directly (DIP: consumers see interfaces in include/revenant).

add_library(revenant STATIC
    carve/BuiltinCarvers.cpp
    carve/CarveMatches.cpp
    carve/CarverRegistry.cpp
    carve/Plausibility.cpp
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
    core/Sha256.cpp
    core/Sha256Block.cpp
    core/Version.cpp
    core/io/ImageFileDeviceShared.cpp
    core/log/LogLevel.cpp
    core/log/Logger.cpp
    core/log/StderrSink.cpp
    fs/BpbFields.cpp
    fs/ExtentLocate.cpp
    fs/Mount.cpp
    fs/MountRegion.cpp
    fs/NameDecode.cpp
    fs/NameEscape.cpp
    fs/SafeArith.cpp
    fs/fat/BootSector.cpp
    fs/fat/BootSectorFields.cpp
    fs/fat/BootSectorGeometry.cpp
    fs/fat/ChainExtents.cpp
    fs/fat/DirectoryBytes.cpp
    fs/fat/DirectoryEntry.cpp
    fs/fat/DirectoryWalk.cpp
    fs/fat/EntryFromSlot.cpp
    fs/fat/Fat32FileSystem.cpp
    fs/fat/FatTable.cpp
    fs/fat/DosTime.cpp
    fs/fat/LongName.cpp
    fs/fat/LongNameAssembly.cpp
    fs/fat/ShortName.cpp
    fs/ntfs/BootSector.cpp
    fs/ntfs/BootSectorFields.cpp
    fs/ntfs/EntryEnumeration.cpp
    fs/ntfs/EntryFromRecord.cpp
    fs/ntfs/EntryPath.cpp
    fs/ntfs/MftAttributes.cpp
    fs/ntfs/MftRecord.cpp
    fs/ntfs/MftRecordAttributes.cpp
    fs/ntfs/MftRecordFixup.cpp
    fs/ntfs/MftTable.cpp
    fs/ntfs/NtfsFileSystem.cpp
    fs/ntfs/Runlist.cpp
    fs/ntfs/RunlistExtents.cpp
    fs/ntfs/RunlistRun.cpp
    volume/PartitionView.cpp
    recovery/Arbitration.cpp
    recovery/ByteAccounting.cpp
    recovery/CandidateIndex.cpp
    recovery/CandidateIndexRead.cpp
    recovery/CandidateRecord.cpp
    recovery/Checkpoint.cpp
    recovery/Disambiguate.cpp
    recovery/ExtractFile.cpp
    recovery/HybridRecovery.cpp
    recovery/Manifest.cpp
    recovery/ManifestJson.cpp
    recovery/IndexingVisitors.cpp
    recovery/RegionSet.cpp
    recovery/ScanRegions.cpp
    recovery/VolumeWalk.cpp
    recovery/OutputName.cpp
    recovery/OutputPath.cpp
    recovery/RecoverySink.cpp
    recovery/OutputPathSegment.cpp
    recovery/WriteOrder.cpp
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
# Internal headers shared across layer directories (e.g. fs/ExtentLocate.hpp)
# are included from the source root; they are not part of the public interface.
target_include_directories(revenant PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(revenant PRIVATE REVENANT_VERSION="${PROJECT_VERSION}")
target_link_libraries(revenant PRIVATE revenant_project_options)

# --- Command-line frontends ---------------------------------------------------
# The frontends' logic lives in a static library so the test binary can drive it
# directly; each executable is a three-line main(). A CLI that can only be tested
# by spawning a process is a CLI that does not get tested. Both binaries share
# everything but their flags and their usage text.

add_library(revenant_cli STATIC
    cli/CarveCli.cpp
    cli/CarveOptions.cpp
    cli/Frontend.cpp
    cli/Interrupt.cpp
    cli/RecoveryOptions.cpp
    cli/RecoveryRun.cpp
    cli/RunDelivery.cpp
    cli/RunSummary.cpp
    cli/Session.cpp
    cli/UndeleteCli.cpp
    cli/UndeleteOptions.cpp
)
target_include_directories(revenant_cli PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(revenant_cli PUBLIC revenant PRIVATE revenant_project_options)

add_executable(revenant-undelete cli/UndeleteMain.cpp)
target_link_libraries(revenant-undelete PRIVATE revenant_cli revenant_project_options)

add_executable(revenant-carve cli/CarveMain.cpp)
target_link_libraries(revenant-carve PRIVATE revenant_cli revenant_project_options)

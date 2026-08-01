# SPDX-License-Identifier: GPL-3.0-or-later
# The shared static core. Frontends and tests link `revenant`; nothing links
# platform code directly (DIP: consumers see interfaces in include/revenant).

add_library(revenant STATIC
    carve/BuiltinCarvers.cpp
    carve/CarveMatches.cpp
    carve/CarverRegistry.cpp
    carve/CpuFeatures.cpp
    carve/PrefilterAvx2.cpp
    carve/Plausibility.cpp
    carve/SignatureScanner.cpp
    carve/SignatureTable.cpp
    carve/WindowMatch.cpp
    carve/formats/AsciiText.cpp
    carve/formats/HeadMatch.cpp
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
    core/NameEscape.cpp
    core/PathPrefix.cpp
    core/SafeArith.cpp
    core/Sha256.cpp
    core/Sha256Block.cpp
    core/TextNumber.cpp
    core/Utf16Name.cpp
    core/Version.cpp
    core/io/CachingDevice.cpp
    core/io/DeviceIdentity.cpp
    core/io/DeviceNumber.cpp
    core/io/ImageFileDeviceShared.cpp
    core/io/MountTable.cpp
    core/io/SysfsFields.cpp
    core/io/SysfsWalk.cpp
    core/io/NativeSourceDevice.cpp
    core/io/RawDeviceShared.cpp
    core/io/RetryingDevice.cpp
    core/io/SourceDevice.cpp
    core/log/LogLevel.cpp
    core/log/Logger.cpp
    core/log/StderrSink.cpp
    fs/BpbFields.cpp
    fs/ClusterChain.cpp
    fs/DirectoryTreeWalk.cpp
    fs/DosTime.cpp
    fs/ExtentLocate.cpp
    fs/ExtentSpan.cpp
    fs/Mount.cpp
    fs/MountRegion.cpp
    fs/PathSafeByte.cpp
    fs/RawName.cpp
    fs/UnixTime.cpp
    fs/VolumeReader.cpp
    fs/ext4/BlockReader.cpp
    fs/ext4/DirectoryBytes.cpp
    fs/ext4/DirectoryEntry.cpp
    fs/ext4/DirectoryHole.cpp
    fs/ext4/DirectoryWalk.cpp
    fs/ext4/EntryFromInode.cpp
    fs/ext4/Ext4FileSystem.cpp
    fs/ext4/ExtentTree.cpp
    fs/ext4/ExtentWalk.cpp
    fs/ext4/GroupDescriptor.cpp
    fs/ext4/Inode.cpp
    fs/ext4/InodeTable.cpp
    fs/ext4/Journal.cpp
    fs/ext4/JournalHint.cpp
    fs/ext4/JournalSuperblock.cpp
    fs/ext4/JournalTags.cpp
    fs/ext4/OrphanList.cpp
    fs/ext4/OrphanPass.cpp
    fs/ext4/Superblock.cpp
    fs/ext4/SuperblockFields.cpp
    fs/ext4/SuperblockGeometry.cpp
    fs/ext4/WalkCursor.cpp
    fs/exfat/AllocationBitmap.cpp
    fs/exfat/BootRegion.cpp
    fs/exfat/BootRegionFields.cpp
    fs/exfat/BootRegionGeometry.cpp
    fs/exfat/DirectoryEntry.cpp
    fs/exfat/EntrySetWalk.cpp
    fs/exfat/ExfatFileSystem.cpp
    fs/exfat/PendingSet.cpp
    fs/fat/BootSector.cpp
    fs/fat/BootSectorFields.cpp
    fs/fat/BootSectorGeometry.cpp
    fs/fat/DirectoryBytes.cpp
    fs/fat/DirectoryEntry.cpp
    fs/fat/DirectoryWalk.cpp
    fs/fat/EntryFromSlot.cpp
    fs/fat/Fat32FileSystem.cpp
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
    volume/ExtendedChain.cpp
    volume/Gpt.cpp
    volume/GptEntry.cpp
    volume/GptEntryArray.cpp
    volume/GptHeaderFields.cpp
    volume/GptPartitions.cpp
    volume/GptPlacement.cpp
    volume/Mbr.cpp
    volume/MbrPartitions.cpp
    volume/MbrPlacement.cpp
    volume/PartitionLabel.cpp
    volume/PartitionTable.cpp
    volume/PartitionView.cpp
    volume/SectorIo.cpp
    recovery/Arbitration.cpp
    recovery/ByteAccounting.cpp
    recovery/CandidateIndex.cpp
    recovery/CandidateIndexRead.cpp
    recovery/CandidateRecord.cpp
    recovery/Checkpoint.cpp
    recovery/DestinationRule.cpp
    recovery/Disambiguate.cpp
    recovery/ExtractFile.cpp
    recovery/HybridRecovery.cpp
    recovery/Manifest.cpp
    recovery/ManifestJson.cpp
    recovery/IndexingVisitors.cpp
    recovery/PartitionedWalk.cpp
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
    target_sources(revenant PRIVATE
        core/io/DeviceIdentityWindows.cpp
        core/io/NativeIoWindows.cpp
        core/io/RawDeviceWindows.cpp
        core/io/DeviceQueryWindows.cpp
        core/io/VolumeExtentsWindows.cpp)
    set_source_files_properties(
        core/io/DeviceIdentityWindows.cpp
        core/io/NativeIoWindows.cpp
        core/io/RawDeviceWindows.cpp
        core/io/DeviceQueryWindows.cpp
        core/io/VolumeExtentsWindows.cpp
        PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN;NOMINMAX")
else()
    target_sources(revenant PRIVATE
        core/io/DeviceIdentityPosix.cpp
        core/io/NativeIoPosix.cpp
        core/io/RawDevicePosix.cpp
        core/io/SysfsStoragePosix.cpp)
endif()

# --- The one translation unit compiled for AVX2 -------------------------------
# Only this file. Building the whole binary with /arch:AVX2 or -mavx2 would
# produce something that crashes on a CPU without it, and the machines people
# run recovery tools on are old machines. Nothing inside it is called before
# carve::cpuHasAvx2() has said yes at run time.
#
# On x86 only: the intrinsics do not exist elsewhere, and a build for another
# architecture reports carve::buildHasAvx2() == false and takes the portable
# path, which is a complete matcher rather than a fallback.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86|x86)$")
    if(MSVC)
        set(revenant_avx2_flags "/arch:AVX2")
    else()
        set(revenant_avx2_flags "-mavx2")
    endif()
    set_source_files_properties(carve/PrefilterAvx2.cpp PROPERTIES
        COMPILE_OPTIONS "${revenant_avx2_flags}"
        COMPILE_DEFINITIONS "REVENANT_HAVE_AVX2=1")
    set_source_files_properties(carve/CpuFeatures.cpp PROPERTIES
        COMPILE_DEFINITIONS "REVENANT_HAVE_AVX2=1")
endif()
# Off x86 the translation unit still compiles and still links: without
# REVENANT_HAVE_AVX2 it holds a stub that carve::buildHasAvx2() reports as
# absent, so nothing ever calls it. A missing symbol would be a link error on
# the architectures this project has not been asked to be fast on yet.

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
    cli/PartitionListing.cpp
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

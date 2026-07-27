// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/fs/ntfs/Runlist.hpp"

namespace revenant::imagegen::ntfs {

// How a file's `$DATA` is stored — or that it has none, as a directory does.
enum class DataKind : std::uint8_t { kNone, kResident, kNonResident };

// One file in the fixture volume: its record, its directory entry, and the
// bytes it holds. Tests read this table instead of restating expectations, so
// the fixture and what is asserted about it cannot drift apart.
struct FixtureFile {
	std::uint64_t recordNumber;
	std::string_view name;
	std::uint64_t parentRecord;
	bool inUse;
	bool isDirectory;
	DataKind dataKind;
	std::vector<revenant::fs::ntfs::DataRun> runs;
	std::vector<std::byte> content;
};

// Record numbers, named so a test never spells out a bare index.
inline constexpr std::uint64_t kMftRecord = 0;
inline constexpr std::uint64_t kRootRecord = 5;
inline constexpr std::uint64_t kPhotosRecord = 16;
inline constexpr std::uint64_t kKeepJpegRecord = 17;
inline constexpr std::uint64_t kDeletedJpegRecord = 18;
inline constexpr std::uint64_t kDeletedNotesRecord = 19;
inline constexpr std::uint64_t kOrphanJpegRecord = 20;

// The parent `orphan.jpg` points at: no record lives there, which is what
// makes the entry orphaned rather than merely deleted.
inline constexpr std::uint64_t kMissingParentRecord = 99;

// A JPEG sits here that no record points at — pure carve territory.
inline constexpr std::uint64_t kUnallocatedJpegCluster = 60;

// A structurally valid JPEG (SOI → EOI) of exactly `sizeBytes`, deterministic
// and free of raw 0xFF bytes in the entropy run, so the carver validates it
// rather than merely finding a header.
[[nodiscard]] std::vector<std::byte> fixtureJpeg(std::size_t sizeBytes);

[[nodiscard]] std::vector<std::byte> unallocatedJpeg();

[[nodiscard]] std::vector<FixtureFile> fixtureFiles(const NtfsLayout& layout);

} // namespace revenant::imagegen::ntfs

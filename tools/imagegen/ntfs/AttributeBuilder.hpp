// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "revenant/fs/ntfs/Runlist.hpp"

namespace revenant::imagegen::ntfs {

// Fixed FILETIMEs (100 ns ticks since 1601). Recognizable rather than
// realistic: a test asserting on them reads as an identity check, and nothing
// in the fixture depends on wall-clock time.
inline constexpr std::uint64_t kFixtureCreated = 0x01D8000000000001ULL;
inline constexpr std::uint64_t kFixtureModified = 0x01D8000000000002ULL;
inline constexpr std::uint64_t kFixtureAccessed = 0x01D8000000000003ULL;

struct FileNameSpec {
	std::uint64_t parentRecord{};
	std::uint16_t parentSequence{};
	std::string_view name; // ASCII; widened to UTF-16LE on the way out
	std::uint64_t realSize{};
};

struct NonResidentDataSpec {
	std::span<const revenant::fs::ntfs::DataRun> runs;
	std::uint64_t realSize{};
	std::uint32_t bytesPerCluster{};
};

// Each builder returns one complete attribute — header plus content, padded to
// the 8-byte multiple the record walker requires — ready to concatenate into a
// record. They are specified against the production attribute readers.
[[nodiscard]] std::vector<std::byte> buildStandardInformation();
[[nodiscard]] std::vector<std::byte> buildFileName(const FileNameSpec& spec);
[[nodiscard]] std::vector<std::byte> buildResidentData(std::span<const std::byte> content);
[[nodiscard]] std::vector<std::byte> buildNonResidentData(const NonResidentDataSpec& spec);

// The 0xFFFFFFFF type code that ends a record's attribute list.
[[nodiscard]] std::vector<std::byte> buildEndMarker();

} // namespace revenant::imagegen::ntfs

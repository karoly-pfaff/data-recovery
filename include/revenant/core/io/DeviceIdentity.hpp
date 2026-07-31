// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <vector>

#include "revenant/core/Result.hpp"

namespace revenant {

// A contiguous byte range on one physical disk. `disk` is whatever the running
// OS calls the whole device the range sits on — a Windows disk number, the
// `dev_t` of a POSIX parent disk — so two extents are comparable within a run
// and mean nothing outside one.
struct StorageExtent {
	std::uint64_t disk;
	std::uint64_t offsetBytes;
	std::uint64_t lengthBytes;

	friend bool operator==(const StorageExtent&, const StorageExtent&) = default;
};

// Where a path's bytes physically are. Empty is a real answer rather than a
// failure: a destination on a network share sits on no local disk at all, and
// ADR-0007 permits one.
using StorageExtents = std::vector<StorageExtent>;

// A whole disk's range, stated without asking how big it is: every byte of it,
// so every volume on it is inside. A size query would add a way to fail to a
// question that does not need one.
inline constexpr std::uint64_t kWholeDisk = std::numeric_limits<std::uint64_t>::max();

// Do the two occupy any of the same bytes of the same disk? This is the whole
// of ADR-0005's destination rule: recovery may not write where it reads, and
// "where" is physical storage rather than a path spelling.
[[nodiscard]] bool
overlaps(std::span<const StorageExtent> source, std::span<const StorageExtent> destination);

// The storage the filesystem holding `directory` is written on.
[[nodiscard]] Result<StorageExtents> storageUnder(const std::filesystem::path& directory);

// The storage a raw device path reads: every byte of a whole disk, or one
// volume's extents. A path that names no device is refused rather than
// answered "nowhere" — an unanswerable identity must not walk a run past the
// destination rule.
[[nodiscard]] Result<StorageExtents> storageOf(const std::filesystem::path& devicePath);

} // namespace revenant

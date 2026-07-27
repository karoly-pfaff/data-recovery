// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ntfs/BootSector.hpp"

namespace revenant::fs::ntfs {

// One decoded data run, in cluster space. A sparse run is a hole in the file:
// it is backed by no clusters at all, so `startCluster` says nothing about it.
struct DataRun {
	std::uint64_t startCluster{};
	std::uint64_t lengthClusters{};
	bool sparse{};
};

// The data runs of one non-resident attribute, in file order.
struct Runlist {
	std::vector<DataRun> runs;
	std::uint64_t totalClusters{};
};

// ADR-0009 bounded allocation: the run count is read from hostile bytes, so it
// may not size a loop or a vector unchecked. A real extent map stays far below
// this — a 1024-byte MFT record cannot even encode half this many runs.
inline constexpr std::size_t kMaxDataRuns = 1024;

// Decodes the data runs of a non-resident attribute. Each run is a header byte
// whose nibbles give the widths of the two fields behind it: an unsigned
// cluster count and a *signed* LCN delta relative to the previous run's start.
// A zero header byte ends the list; bytes that run out before it are
// kOutOfRange, and any malformed width, length, or delta is kInvalidArgument.
// Decoding is purely structural — nothing here knows the volume geometry.
[[nodiscard]] Result<Runlist> decodeRunlist(std::span<const std::byte> runlistBytes);

// Maps decoded runs onto device-relative byte extents and trims the tail to
// `realSize`, the attribute's declared content length. A run reaching past the
// volume, a `realSize` larger than the runs allocate, or a sparse run (which
// has no bytes to point at) is kInvalidArgument — such a file is the carve
// pass's problem, not something to approximate here.
[[nodiscard]] Result<std::vector<Extent>>
runlistExtents(const Runlist& runlist, const NtfsGeometry& geometry, std::uint64_t realSize);

} // namespace revenant::fs::ntfs

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Cluster numbers restated as the device byte ranges that hold a
// file's content. Not a public interface.

#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/fat/BootSector.hpp"

namespace revenant::fs::fat {

// Where `cluster` begins on the device.
[[nodiscard]] std::uint64_t clusterOffset(const Fat32Geometry& geometry, std::uint32_t cluster);

// `clusters` as device extents, with consecutive clusters coalesced into one
// extent and the last one trimmed so the extents sum to `sizeBytes`.
//
// A `sizeBytes` larger than the clusters hold is `kInvalidArgument`: a
// directory entry that claims more bytes than its own chain allocates is not
// describing a file this can hand back.
[[nodiscard]] Result<std::vector<Extent>> chainExtents(
	std::span<const std::uint32_t> clusters,
	const Fat32Geometry& geometry,
	std::uint64_t sizeBytes);

// The contiguous run a `sizeBytes` file starting at `first` would occupy.
//
// This is what a *deleted* file gets. Deletion frees the file's FAT entries, so
// the chain that said where its later clusters went is gone and nothing on the
// volume holds it. Contiguity is the only guess left — right for an
// unfragmented file, wrong for a fragmented one — which is why an entry read
// this way is never graded better than `kUncertain`.
[[nodiscard]] Result<std::vector<Extent>>
contiguousExtents(std::uint32_t first, const Fat32Geometry& geometry, std::uint64_t sizeBytes);

} // namespace revenant::fs::fat

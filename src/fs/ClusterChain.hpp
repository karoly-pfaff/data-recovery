// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Following a cluster chain and restating it as device extents.
//
// FAT32 and exFAT differ in how they *describe* a volume — counts against log2
// exponents — but not in how a chain works once the description is parsed: a
// table of 32-bit entries, cluster numbering from 2, and an end-of-chain marker
// at 0x0FFFFFF8. So the following is written once, against the handful of
// numbers both geometries reduce to. Not a public interface.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fs/VolumeReader.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::fs {

// Cluster numbering starts at 2: entries 0 and 1 of the table are reserved and
// hold no file.
inline constexpr std::uint32_t kFirstCluster = 2;

// Only the low 28 bits of an entry are a cluster number; the top four are
// reserved and must be masked off rather than read as part of one.
inline constexpr std::uint32_t kClusterMask = 0x0FFF'FFFF;

// At or above this an entry ends the chain. Below it, 0x0FFFFFF7 marks a bad
// cluster and 0 marks a free one — a chain may run into neither.
inline constexpr std::uint32_t kEndOfChain = 0x0FFF'FFF8;

// What following a chain needs, whichever filesystem described it.
struct ClusterGeometry {
	std::uint32_t bytesPerCluster;
	std::uint64_t tableOffsetBytes;
	std::uint64_t tableSizeBytes;
	// Where cluster `kFirstCluster` begins.
	std::uint64_t dataOffsetBytes;
	std::uint64_t totalClusters;
};

// The table as a chain-follower. Read-only throughout (ADR-0005); the device is
// borrowed, never owned, and must outlive it.
class ClusterChain {
public:
	ClusterChain(BlockDevice& device, const ClusterGeometry& geometry) noexcept;

	[[nodiscard]] const ClusterGeometry& geometry() const noexcept;

	// Whether `value` names a cluster the data region actually holds, as
	// opposed to ending a chain or pointing outside the volume.
	[[nodiscard]] bool isDataCluster(std::uint32_t value) const noexcept;

	[[nodiscard]] Result<std::uint32_t> entryAt(std::uint32_t cluster) const;

	// Every cluster a file starting at `first` occupies, in order.
	//
	// A chain that runs into a free entry, a bad cluster, or a number outside
	// the data region is `kInvalidArgument` — that is what a *freed* chain looks
	// like, and reporting it as a shorter file would hand back the wrong bytes.
	// A chain longer than the volume has clusters is `kOutOfRange`, so a crafted
	// cycle cannot hang a walk (ADR-0009).
	[[nodiscard]] Result<std::vector<std::uint32_t>> chainFrom(std::uint32_t first) const;

	// Fills `buffer` from `offset`. A short read is `kOutOfRange`: the volume
	// ends inside what was asked for, which makes it unreadable, not empty.
	[[nodiscard]] Result<std::size_t> read(std::uint64_t offset, std::span<std::byte> buffer) const;

private:
	VolumeReader reader_;
	ClusterGeometry geometry_;
};

// Where `cluster` begins on the device.
[[nodiscard]] std::uint64_t clusterOffset(const ClusterGeometry& geometry, std::uint32_t cluster);

// `clusters` as device extents, consecutive clusters coalesced into one and the
// last trimmed so the extents sum to `sizeBytes`. A `sizeBytes` larger than the
// clusters hold is `kInvalidArgument`.
[[nodiscard]] Result<std::vector<Extent>> chainExtents(
	std::span<const std::uint32_t> clusters,
	const ClusterGeometry& geometry,
	std::uint64_t sizeBytes);

// The contiguous run a `sizeBytes` file starting at `first` would occupy.
//
// exFAT states this as a fact when a file's `NoFatChain` flag is set. FAT32 has
// to *assume* it for a deleted file, whose chain was freed — right for an
// unfragmented file, wrong for a fragmented one, which is why an entry read
// that way is never graded better than uncertain.
[[nodiscard]] Result<std::vector<Extent>>
contiguousExtents(std::uint32_t first, const ClusterGeometry& geometry, std::uint64_t sizeBytes);

} // namespace revenant::fs

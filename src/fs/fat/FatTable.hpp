// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The File Allocation Table as a chain-follower: given a file's first
// cluster, which clusters hold the rest of it. `fs::mountVolume` is the only
// public door onto FAT32, so nothing outside `fs::fat` names this.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/fat/BootSector.hpp"

namespace revenant::fs::fat {

// FAT32 stores cluster numbers in the low 28 bits of a 32-bit entry; the top
// four are reserved and must be masked off rather than read as part of one.
inline constexpr std::uint32_t kClusterMask = 0x0FFF'FFFF;

// At or above this, an entry ends the chain. Below it, 0x0FFFFFF7 marks a bad
// cluster and 0 marks a free one — a chain may run into neither.
inline constexpr std::uint32_t kEndOfChain = 0x0FFF'FFF8;

// Read-only throughout (ADR-0005); the device is borrowed, never owned, and
// must outlive the table.
class FatTable {
public:
	FatTable(BlockDevice& device, const Fat32Geometry& geometry) noexcept;

	[[nodiscard]] const Fat32Geometry& geometry() const noexcept;

	// Whether `value` names a cluster the data region actually holds, as
	// opposed to ending a chain or pointing outside the volume.
	[[nodiscard]] bool isDataCluster(std::uint32_t value) const noexcept;

	// The FAT entry for `cluster`, masked to its 28 significant bits.
	[[nodiscard]] Result<std::uint32_t> entryAt(std::uint32_t cluster) const;

	// Every cluster a file starting at `first` occupies, in order.
	//
	// A chain that runs into a free entry, a bad cluster, or a number outside
	// the data region is `kInvalidArgument` — that is what a *freed* chain looks
	// like, and reporting it as a shorter file would hand back the wrong bytes.
	// A chain longer than the volume has clusters is `kOutOfRange`, so a crafted
	// cycle cannot hang the walk (ADR-0009).
	[[nodiscard]] Result<std::vector<std::uint32_t>> chainFrom(std::uint32_t first) const;

	// Fills `buffer` from `offset`, or says why it could not. Reading a
	// directory's clusters needs the device, and the table is what borrows it —
	// so nothing outside holds one. A short read is `kOutOfRange`: the volume
	// ends inside what was asked for, which makes it unreadable, not empty.
	[[nodiscard]] Result<std::size_t> read(std::uint64_t offset, std::span<std::byte> buffer) const;

private:
	BlockDevice* device_; // non-owning, never null
	Fat32Geometry geometry_;
};

} // namespace revenant::fs::fat

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The volume as blocks: where one is, whether it exists, and its
// bytes. Everything else under `fs/ext4` addresses the device through this, so
// no other file multiplies a block number by a block size. Not a public
// interface.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fs/VolumeReader.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ext4/Superblock.hpp"

namespace revenant::fs::ext4 {

// Read-only throughout (ADR-0005); the device is borrowed, never owned, and
// must outlive this.
class Ext4Blocks {
public:
	Ext4Blocks(BlockDevice& device, const Ext4Geometry& geometry) noexcept;

	[[nodiscard]] const Ext4Geometry& geometry() const noexcept;

	// Whether `block` is one the volume actually holds. Block 0 is only a data
	// block on a volume whose superblock does not occupy it, which is what
	// `firstDataBlock` records.
	[[nodiscard]] bool isDataBlock(std::uint64_t block) const noexcept;

	[[nodiscard]] std::uint64_t blockOffset(std::uint64_t block) const noexcept;

	// One block's bytes. A block the volume does not hold is kOutOfRange before
	// the device is touched at all.
	[[nodiscard]] Result<std::vector<std::byte>> readBlock(std::uint64_t block) const;

	// Fills `buffer` from `offset`. A short read is kOutOfRange: the volume ends
	// inside what was asked for, which makes it unreadable, not empty.
	[[nodiscard]] Result<std::size_t> read(std::uint64_t offset, std::span<std::byte> buffer) const;

private:
	VolumeReader reader_;
	Ext4Geometry geometry_;
};

// The bytes `extents` cover, up to `capBytes`.
//
// Content bigger than the cap is read up to it and no further: what a volume
// claims about the size of its own directories is data like any other
// (ADR-0009). A short read means the volume ends inside the content, which makes
// the rest unreadable rather than empty.
[[nodiscard]] Result<std::vector<std::byte>>
readExtents(const Ext4Blocks& blocks, std::span<const Extent> extents, std::size_t capBytes);

} // namespace revenant::fs::ext4

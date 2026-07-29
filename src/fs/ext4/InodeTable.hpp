// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Finding the inode a number names. ext4 spreads its inodes over one
// table per block group, and which block that table starts in is the group
// descriptor's to say — so reaching an inode is two reads, and this is where
// both live. Not a public interface.

#include <cstdint>

#include "fs/ext4/BlockReader.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/ext4/Inode.hpp"

namespace revenant::fs::ext4 {

class Ext4InodeTable {
public:
	explicit Ext4InodeTable(const Ext4Blocks& blocks) noexcept;

	// Where inode `number` begins on the device.
	//
	// A number outside `[1, s_inodes_count]`, or one whose group descriptor
	// names a table block the volume does not hold, is kOutOfRange: an inode
	// number off a disk addresses nothing until it has been checked against the
	// volume's own counts.
	[[nodiscard]] Result<std::uint64_t> offsetOf(std::uint32_t number) const;

	[[nodiscard]] Result<Ext4Inode> read(std::uint32_t number) const;

private:
	const Ext4Blocks* blocks_; // non-owning, never null
};

} // namespace revenant::fs::ext4

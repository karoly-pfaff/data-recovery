// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The one thing the journal is consulted for: an extent tree a
// deletion wiped.
//
// Many kernels zero an inode's `i_block` when they free it, so the name survives
// in its directory's hole and the inode survives in its table, but nothing says
// where the bytes were. ext4 journals metadata, so the inode's *table block* was
// written into the journal on its way to disk — and a block freed today may sit
// there beside an older copy of itself, from a transaction before the deletion,
// whose tree is intact. Not a public interface.

#include <cstdint>
#include <vector>

#include "fs/ext4/BlockReader.hpp"
#include "fs/ext4/InodeTable.hpp"
#include "fs/ext4/Journal.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::fs::ext4 {

// What the hint reads through. All three are borrowed and must outlive it.
struct JournalSource {
	const Ext4Blocks* blocks;
	const Ext4InodeTable* inodes;
	const Journal* journal;
};

// The extents an older copy of inode `number` still names, or `kNotFound` when
// the journal holds no copy that maps anything.
//
// The extents are real but *stale*: they say where the file's blocks were when
// that copy was written, not that those blocks still hold it. An entry recovered
// this way is never graded better than uncertain, and the carve pass is what
// settles the rest.
[[nodiscard]] Result<std::vector<Extent>>
journalExtents(const JournalSource& source, std::uint32_t number, std::uint64_t sizeBytes);

} // namespace revenant::fs::ext4

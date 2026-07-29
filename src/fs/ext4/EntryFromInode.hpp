// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Turning a name and the inode it points at into the entry the layer
// reports. Where the content is looked for — and when it deliberately is not —
// lives here. Not a public interface.

#include <cstdint>
#include <string>

#include "fs/ext4/BlockReader.hpp"
#include "fs/ext4/InodeTable.hpp"
#include "fs/ext4/Journal.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ext4/Inode.hpp"

namespace revenant::fs::ext4 {

// Everything an entry is built through. All borrowed; all must outlive it.
struct EntrySource {
	const Ext4Blocks* blocks;
	const Ext4InodeTable* inodes;
	const Journal* journal;
};

// One name the walk found and where it found it.
struct FoundName {
	std::string path;
	std::uint32_t inodeNumber{};
	EntryState state{};
	// False when the name did not survive decoding intact (ADR-0010), which is
	// enough on its own to keep an entry off the top grade.
	bool nameIsExact{};
};

// The reported entry, with its content located.
//
// A live file is located through its own extent tree. A *deleted* one is too —
// until one of two things stops it. If the inode's link count has come back up,
// something else owns those blocks now: the name is still a fact and the entry
// is still reported, but with no extents, because handing back a live file's
// bytes would be worse than handing back none. And if the deletion zeroed the
// tree, as many kernels do, the journal is asked whether it still holds an older
// copy of the inode — and the entry gets those extents, stale but real, or none
// at all. Either way the region is what the carve pass is for.
[[nodiscard]] RecoveredEntry
entryOf(const EntrySource& source, const FoundName& found, const Ext4Inode& inode);

} // namespace revenant::fs::ext4

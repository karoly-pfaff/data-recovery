// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. A parsed short entry plus the place the walk found it, restated in
// the shared `fs::RecoveredEntry` vocabulary. Not a public interface.

#include <string>

#include "fs/ClusterChain.hpp"
#include "revenant/core/Utf16Name.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/fat/DirectoryEntry.hpp"

namespace revenant::fs::fat {

// Where the walk found an entry and what it is called. `underDeleted` says that
// something above it in the tree was deleted, which is what makes it an orphan:
// its name is real, its place in the tree is a guess.
struct EntryPlace {
	std::string path;
	DecodedName name;
	bool underDeleted;
};

// Builds the reported entry, locating its content along the way.
//
// A live file's bytes come from its FAT chain. A deleted file's chain was freed
// on deletion, so the contiguous run its size needs is the only guess left, and
// the entry is graded no better than `kUncertain` because of it. Content that
// cannot be located at all leaves the extents empty — that region is what the
// carve pass is for.
[[nodiscard]] RecoveredEntry
entryFromSlot(const ClusterChain& table, const ShortEntry& entry, const EntryPlace& place);

} // namespace revenant::fs::fat

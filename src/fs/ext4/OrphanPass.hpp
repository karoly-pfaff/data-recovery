// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The pass the directory walk cannot make: the inodes nothing in the
// tree points at. An orphan was unlinked while it was still open, so no
// directory entry names it anywhere and none ever will again — which is why it
// is reached from the superblock rather than from a directory, and why it comes
// back by number. Not a public interface.

#include <cstdint>
#include <span>

#include "fs/ext4/EntryFromInode.hpp"
#include "revenant/fs/RecoveredEntry.hpp"

namespace revenant::fs::ext4 {

// What the pass cost and what it found, in the units the walk's own stats are
// kept in.
struct OrphanPassResult {
	std::uint64_t scanned;
	std::uint64_t reported;
};

// Reports every inode on the volume's orphan list that `alreadyReported` does
// not already hold. An orphan a directory's hole gave a name to is the same
// file, and reporting it twice would double it in the manifest.
[[nodiscard]] OrphanPassResult reportOrphans(
	const EntrySource& source,
	EntryVisitor& visitor,
	std::span<const std::uint32_t> alreadyReported);

} // namespace revenant::fs::ext4

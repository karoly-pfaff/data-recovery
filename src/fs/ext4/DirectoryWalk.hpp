// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The ext4 directory tree, walked from the root inode down, and the
// orphan list after it. Not a public interface: `fs::mountVolume` is the only
// door onto this.

#include "fs/ext4/EntryFromInode.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/RecoveredEntry.hpp"

namespace revenant::fs::ext4 {

// Every number the walk trusts came off the disk, so the depth it will descend
// to is bounded (ADR-0009); how much of one directory it will read is
// `kMaxDirectoryBytes`, beside the reading.
inline constexpr unsigned kMaxDirectoryDepth = 64;

// Walks every directory on the volume, reports each file — live or deleted — to
// `visitor`, and finishes with the orphan list. Discovery only; nothing is
// extracted (ADR-0006).
//
// ext4 directories carry `.` and `..`, so unlike exFAT the walk needs a rule for
// them: they are the directory's own place in the tree rather than files in it,
// and are neither reported nor followed.
[[nodiscard]] Result<EnumerationStats> walkVolume(const EntrySource& source, EntryVisitor& visitor);

} // namespace revenant::fs::ext4

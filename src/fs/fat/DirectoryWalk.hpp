// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The FAT32 directory tree, walked from the root down. Not a public
// interface: `fs::mountVolume` is the only door onto this.

#include <cstddef>

#include "fs/fat/FatTable.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/RecoveredEntry.hpp"

namespace revenant::fs::fat {

// Every number the walk trusts came off the disk, so both are bounded
// (ADR-0009). A FAT tree deeper than this, or a directory larger than this, is
// not a filesystem — it is a volume built to exhaust whoever reads it.
inline constexpr unsigned kMaxDirectoryDepth = 64;
inline constexpr std::size_t kMaxDirectoryBytes = 4U << 20U;

// Walks every directory on the volume and reports each file — live, deleted, or
// orphaned — to `visitor`. Discovery only; nothing is extracted (ADR-0006).
[[nodiscard]] Result<EnumerationStats> walkVolume(const FatTable& table, EntryVisitor& visitor);

} // namespace revenant::fs::fat

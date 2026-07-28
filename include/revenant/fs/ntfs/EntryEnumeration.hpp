// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "revenant/core/Result.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/ntfs/MftTable.hpp"

namespace revenant::fs::ntfs {

// NTFS reserves records 0-15 for its own metadata files (`$MFT`, `$LogFile`,
// `$Bitmap`, ...). They are the filesystem's bookkeeping, not anything a user
// asked to get back, so a user-file walk starts past them.
inline constexpr std::uint64_t kFirstUserRecord = 16;

// Walks every user record in `table` and reports each file — live, deleted, or
// orphaned — to `visitor`.
//
// A record slot that will not parse is skipped rather than fatal: it may be
// empty, or its metadata may be destroyed, and either way that region is what
// the carve pass exists for. A device read fault is fatal, because a disk that
// will not read is not a disk with no files on it.
//
// Discovery only: nothing is extracted and nothing is written (ADR-0006).
[[nodiscard]] Result<EnumerationStats>
enumerateEntries(const MftTable& table, EntryVisitor& visitor);

} // namespace revenant::fs::ntfs

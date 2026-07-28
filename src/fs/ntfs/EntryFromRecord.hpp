// SPDX-License-Identifier: GPL-3.0-or-later
// Internal. Turns one parsed MFT record plus its resolved path into the
// filesystem layer's `RecoveredEntry`. Not a public interface: enumeration is
// the only caller, and building an entry out of context means nothing.
#pragma once

#include "fs/ntfs/EntryPath.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/ntfs/BootSector.hpp"
#include "revenant/fs/ntfs/MftRecord.hpp"

namespace revenant::fs::ntfs {

// Classifies `view` and resolves where its content lives. A record whose
// `$DATA` is missing or whose runlist will not map onto the volume yields an
// entry with no content and an `Uncertain` grade — never approximated bytes.
[[nodiscard]] RecoveredEntry
entryFromRecord(const MftRecordView& view, const EntryPath& path, const NtfsGeometry& geometry);

} // namespace revenant::fs::ntfs

// SPDX-License-Identifier: GPL-3.0-or-later
// Internal. Rebuilds an entry's volume-relative path from the `$FILE_NAME`
// parent references in the MFT. Not a public interface: enumeration is the
// only caller, and a path on its own is not a recovery.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "revenant/fs/ntfs/MftRecord.hpp"
#include "revenant/fs/ntfs/MftTable.hpp"

namespace revenant::fs::ntfs {

// NTFS reserves record 5 for the volume root directory; it is its own parent,
// which is what makes it the place a path walk stops.
inline constexpr std::uint64_t kRootRecordNumber = 5;

// The parent chain is on-disk data, so it may be a cycle or an arbitrarily deep
// forgery (ADR-0009). Real trees are an order of magnitude shallower than this.
inline constexpr std::size_t kMaxPathDepth = 64;

// A rebuilt location. `reachedRoot` is false when the chain broke before record
// 5 — the entry is orphaned, and `path` is only the part that survived.
struct EntryPath {
	std::string path;
	bool reachedRoot;
};

// The name a record is known by: the long name in preference to a DOS 8.3
// alias, which is a compatibility shadow of the same file. Null when the record
// carries no `$FILE_NAME` at all. The pointer is into `names` and borrows its
// lifetime.
[[nodiscard]] const MftFileName* preferredName(const std::vector<MftFileName>& names);

// Rebuilds `view`'s path by walking its parent references up through `table`,
// joining the directory names with `/`. The root contributes no segment, so a
// file directly under it resolves to its bare name.
[[nodiscard]] EntryPath resolveEntryPath(const MftTable& table, const MftRecordView& view);

} // namespace revenant::fs::ntfs

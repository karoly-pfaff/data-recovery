// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The exFAT directory tree, walked from the root down. Not a public
// interface: `fs::mountVolume` is the only door onto this.

#include <cstddef>
#include <cstdint>

#include "fs/ClusterChain.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/RecoveredEntry.hpp"

namespace revenant::fs::exfat {

// Every number the walk trusts came off the disk, so both are bounded
// (ADR-0009).
inline constexpr unsigned kMaxDirectoryDepth = 64;
inline constexpr std::size_t kMaxDirectoryBytes = 4U << 20U;

// Walks every directory on the volume and reports each file — live or deleted —
// to `visitor`. Discovery only; nothing is extracted (ADR-0006).
//
// exFAT has no `.` or `..` entries, so a directory names only its children and
// the walk needs no rule to avoid climbing back up.
[[nodiscard]] Result<EnumerationStats>
walkVolume(const ClusterChain& chain, std::uint32_t rootCluster, EntryVisitor& visitor);

} // namespace revenant::fs::exfat

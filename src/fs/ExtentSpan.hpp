// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. What every filesystem does with a file's layout once it has been
// reduced to device byte ranges: coalesce the runs that touch, total them, and
// cut the tail back to the size the file declares. NTFS's runlists, FAT's
// cluster chains and ext4's extent trees differ in how they *encode* that
// layout, not in this. Not a public interface.

#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::fs {

// Appends `run`, merging it into the previous extent when it begins exactly
// where that one ends. A file laid out in consecutive blocks is one extent, not
// one extent per block.
void appendExtent(std::vector<Extent>& extents, const Extent& run);

[[nodiscard]] std::uint64_t extentBytes(std::span<const Extent> extents) noexcept;

// `extents` cut back to exactly `sizeBytes`: the last extent still needed is
// shortened and everything past it dropped. A file never fills its last block
// exactly, and handing back the slack would append whatever the volume happened
// to leave there.
//
// kInvalidArgument when the extents do not reach `sizeBytes` — runs that
// allocate less than the file claims are not runs that can be handed back as
// the file.
[[nodiscard]] Result<std::vector<Extent>>
trimToSize(std::span<const Extent> extents, std::uint64_t sizeBytes);

} // namespace revenant::fs

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. An inode's extent tree followed to its leaves and restated as
// device extents. Interior nodes live in blocks of their own, so this is where
// the tree stops being a parse and becomes a walk. Not a public interface.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fs/ext4/BlockReader.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ext4/Inode.hpp"

namespace revenant::fs::ext4 {

// A tree with more nodes than this is not a file's layout, it is a volume built
// to make a walk read blocks forever (ADR-0009). Five hundred nodes address
// more blocks than any file on a volume this build reads.
inline constexpr std::size_t kMaxExtentNodes = 512;

// Every device extent the content mapped by `root` occupies, in file order.
//
// A tree that will not parse, or whose extents leave a gap in the file's block
// numbering, are unwritten, or reach past the volume, is `kInvalidArgument` —
// no extents rather than the wrong ones. ext4 can allocate blocks without
// writing them and can leave holes; `RecoveredEntry`'s extents are a
// concatenation and can spell neither, and padding a recovered file with
// whatever those blocks last held would be worse than locating nothing. NTFS's
// sparse runs already carry this rule.
[[nodiscard]] Result<std::vector<Extent>>
treeExtents(const Ext4Blocks& blocks, std::span<const std::byte> root, std::uint64_t sizeBytes);

// The same for an inode's own tree. An inode whose block map is ext2's indirect
// list rather than an extent tree is `kInvalidArgument`: this build reads extent
// trees, and that region is what the carve pass is for.
[[nodiscard]] Result<std::vector<Extent>>
inodeExtents(const Ext4Blocks& blocks, const Ext4Inode& inode);

} // namespace revenant::fs::ext4

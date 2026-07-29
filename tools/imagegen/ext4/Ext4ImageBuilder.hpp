// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <vector>

namespace revenant::imagegen::ext4 {

// Builds the whole synthetic ext4 volume in memory: superblock, group
// descriptor, inode table, the root and one subdirectory with their deletions
// already swallowed, every fixture file's content in the blocks the layout gives
// it, an inode on the orphan list, and a journal still holding the extent tree
// one of those deletions wiped. Identical every run.
[[nodiscard]] std::vector<std::byte> buildExt4Image();

} // namespace revenant::imagegen::ext4

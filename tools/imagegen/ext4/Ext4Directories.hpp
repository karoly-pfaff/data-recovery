// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <vector>

#include "imagegen/ext4/Ext4Layout.hpp"

namespace revenant::imagegen::ext4 {

// The root directory's block, exactly as ext4 leaves one after three deletions.
//
// Nothing marks a deleted entry here. Each one is still lying where it was
// written; what changed is the *previous* entry's record length, which was grown
// to swallow it, so a reader walking record to record steps over it and never
// sees it again. Building the block this way is the whole point of the fixture:
// the walk has to search those holes to find the three names back.
[[nodiscard]] std::vector<std::byte> rootDirectoryBlock(const Ext4Layout& layout);

// The `photos` directory: `.`, `..`, and one live file.
[[nodiscard]] std::vector<std::byte> photosDirectoryBlock(const Ext4Layout& layout);

} // namespace revenant::imagegen::ext4

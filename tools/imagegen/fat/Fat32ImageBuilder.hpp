// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <vector>

namespace revenant::imagegen::fat {

// Builds the whole synthetic FAT32 volume in memory: boot sector, two FATs,
// the root and its subdirectories, and every fixture file's content in the
// clusters the layout gives it. Identical every run.
[[nodiscard]] std::vector<std::byte> buildFat32Image();

} // namespace revenant::imagegen::fat

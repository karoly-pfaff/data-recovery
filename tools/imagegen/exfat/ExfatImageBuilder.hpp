// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <vector>

namespace revenant::imagegen::exfat {

// Builds the whole synthetic exFAT volume in memory: boot sector, FAT,
// allocation bitmap, the root and one subdirectory, and every fixture file's
// content in the clusters the layout gives it. Identical every run.
[[nodiscard]] std::vector<std::byte> buildExfatImage();

} // namespace revenant::imagegen::exfat

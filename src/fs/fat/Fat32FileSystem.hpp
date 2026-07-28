// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. FAT32 behind the shared filesystem seam: a volume whose FAT is
// located once and whose directory tree is walked on demand. `fs::mountVolume`
// is the only door — nothing outside the mount table names this.

#include <memory>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/FileSystem.hpp"

namespace revenant::fs::fat {

// Offers `device` to FAT32. A boot sector that does not carry the `FAT32   `
// type string belongs to some other filesystem, so it is declined with
// kNotFound; one that does carry it is mounted, or fails with the BPB parser's
// own typed error.
[[nodiscard]] Result<std::unique_ptr<FileSystem>> mountFat32(BlockDevice& device);

} // namespace revenant::fs::fat

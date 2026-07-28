// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. exFAT behind the shared filesystem seam. `fs::mountVolume` is the
// only door — nothing outside the mount table names this.

#include <memory>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/FileSystem.hpp"

namespace revenant::fs::exfat {

// Offers `device` to exFAT. A boot sector that does not name exFAT — or that
// keeps geometry where exFAT writes zeros — belongs to some other filesystem,
// so it is declined with kNotFound; one that does is mounted, or fails with the
// boot-region parser's own typed error.
[[nodiscard]] Result<std::unique_ptr<FileSystem>> mountExfat(BlockDevice& device);

} // namespace revenant::fs::exfat

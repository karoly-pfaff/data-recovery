// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. ext4 behind the shared filesystem seam. `fs::mountVolume` is the
// only door — nothing outside the mount table names this.

#include <memory>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/FileSystem.hpp"

namespace revenant::fs::ext4 {

// Offers `device` to ext4. A volume whose superblock does not carry ext4's magic
// a kilobyte in — or carries it beside a block size ext4 cannot express — is
// declined with kNotFound; one that does is mounted, or fails with the
// superblock parser's own typed error.
[[nodiscard]] Result<std::unique_ptr<FileSystem>> mountExt4(BlockDevice& device);

} // namespace revenant::fs::ext4

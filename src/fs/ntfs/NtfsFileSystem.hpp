// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. NTFS behind the shared filesystem seam: the `$MFT` mounted once and
// walked on demand. `fs::mountVolume` is the only door — nothing outside the
// mount table names this. Not a public interface.

#include <memory>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/FileSystem.hpp"

namespace revenant::fs::ntfs {

// Offers `device` to NTFS. A boot sector that does not carry NTFS's OEM id
// belongs to some other filesystem, so it is declined with kNotFound rather
// than rejected; one that does carry it is mounted, or fails with the
// boot-sector parser's own typed error.
[[nodiscard]] Result<std::unique_ptr<FileSystem>> mountNtfs(BlockDevice& device);

} // namespace revenant::fs::ntfs

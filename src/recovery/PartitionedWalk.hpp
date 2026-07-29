// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Walking a whole disk as all of its volumes: each partition is
// mounted through its own window, and everything it reports comes back out in
// the disk's own coordinates. Not a public interface.

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/RecoveredEntry.hpp"

namespace revenant::recovery {

// Mounts and walks every partition `device`'s table describes, reporting into
// `visitor` with device-relative extents and partition-qualified paths.
//
// A device with no partition table — an image of a single volume, the ordinary
// case — is walked whole, exactly as `enumerateVolume` walks it, with the paths
// it has always had.
//
// A partition that will not mount is skipped: a swap partition, an EFI system
// partition this build cannot read, a volume whose superblock is gone are all
// normal on a real disk and none is a reason to abandon the volumes that did
// mount. A read *fault* is still fatal, because a disk that will not read is not
// a disk with no files.
[[nodiscard]] Result<fs::EnumerationStats>
enumerateDisk(BlockDevice& device, fs::EntryVisitor& visitor);

} // namespace revenant::recovery

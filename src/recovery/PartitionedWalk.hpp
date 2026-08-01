// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Walking a whole disk as all of its volumes: each partition is
// mounted through its own window, and everything it reports comes back out in
// the disk's own coordinates. Not a public interface.

#include <span>

#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/volume/PartitionTable.hpp"

namespace revenant::recovery {

// Mounts and walks exactly the `partitions` it is given, in the coordinates of
// the `device` it is given, reporting into `visitor` with device-relative
// extents and partition-qualified paths.
//
// It reads no partition table of its own: which partitions a run walks is
// `RunScope`'s answer, decided once from one reading of the source. Handing them
// in is what stops a volume from being asked whether it is a disk.
//
// A partition that will not mount is skipped: a swap partition, an EFI system
// partition this build cannot read, a volume whose superblock is gone are all
// normal on a real disk and none is a reason to abandon the volumes that did
// mount. That leaves nothing here that can fail, so nothing here returns a
// failure.
[[nodiscard]] fs::EnumerationStats enumerateDisk(
	BlockDevice& device,
	std::span<const volume::Partition> partitions,
	fs::EntryVisitor& visitor);

} // namespace revenant::recovery

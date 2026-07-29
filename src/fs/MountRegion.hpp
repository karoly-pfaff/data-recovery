// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The bytes a mounter needs before it can recognize a volume, read
// once for every filesystem rather than once per filesystem. Not a public
// interface.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::fs {

// Where a filesystem names itself. NTFS, FAT32 and exFAT all do so in sector 0;
// ext4 leaves the first kilobyte to a boot loader and starts after it, which is
// why the offset is stated rather than assumed. Named fields, because two bare
// numbers in a row are two numbers waiting to be swapped.
struct MountRegion {
	std::uint64_t offset;
	std::size_t length;
};

// The region's bytes, or the reason there are none.
//
// A device too short to hold them carries no filesystem at all, so that is
// `kNotFound` — the mount table's "keep looking" — rather than a truncated
// read. A device that faults keeps its own `kIoFailure`: a disk that will not
// answer is not a disk with nothing on it.
[[nodiscard]] Result<std::vector<std::byte>>
readMountRegion(BlockDevice& device, const MountRegion& region);

} // namespace revenant::fs

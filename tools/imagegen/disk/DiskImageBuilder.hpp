// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace revenant::imagegen::disk {

// A whole synthetic disk and where its volumes ended up. The offsets are
// reported rather than recomputed by the caller: a test that derived them again
// would be asserting its own arithmetic instead of the builder's.
struct DiskImage {
	std::vector<std::byte> bytes;
	std::vector<std::uint64_t> volumeOffsets;
};

// A disk whose partition table is an MBR and whose partitions are the four
// filesystem fixtures — NTFS, FAT32, exFAT and ext4 — in that order, each
// aligned to the 1 MiB boundary a modern partitioner uses. Identical every run.
[[nodiscard]] DiskImage buildMbrDiskImage();

} // namespace revenant::imagegen::disk

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "revenant/core/Result.hpp"

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

// The same disk, with a second partition table written into partition 1's own
// first sector — the 66 bytes a real volume fills with bootstrap code and a
// signature, which is what makes them indistinguishable from a table. One slot
// is used and places a window inside the volume, so the phantom is well formed
// rather than damage: `0x1BE-0x1FD` holds none of NTFS's BPB fields, and the
// volume mounts and reads exactly as it does on `buildMbrDiskImage()`.
[[nodiscard]] DiskImage buildPhantomTableDiskImage();

// That disk, written to `path`; returns the bytes written. The volume offsets
// are not reported here — a file on disk is read back by the partition table
// it carries, which is the whole point of the fixture.
[[nodiscard]] Result<std::uint64_t> writeMbrDiskImage(const std::filesystem::path& path);

} // namespace revenant::imagegen::disk

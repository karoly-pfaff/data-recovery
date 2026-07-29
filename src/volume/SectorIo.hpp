// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Reading a partition table's sectors off a device: turning an LBA
// into a byte offset, and pulling in the 512 bytes a table sector occupies.
// Both bound themselves to the device before they compute anything, which is
// what makes the arithmetic safe rather than merely checked — an LBA the device
// can address cannot overflow a byte offset. Not a public interface.

#include <array>
#include <cstddef>
#include <cstdint>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/volume/Mbr.hpp"

namespace revenant::volume {

// The 512 bytes a partition table's opening sector occupies, read out whole: an
// MBR, an EBR, or the sector a GPT header sits in. A GPT header is at least 92
// bytes and every writer emits exactly that, so 512 covers it even where the
// device's own sectors are larger.
using TableSector = std::array<std::byte, kMbrSectorBytes>;

// Where sector `lba` begins, in bytes. A device reporting a zero sector size is
// kInvalidArgument; an LBA with no room for a whole sector before the end of the
// device is kOutOfRange at the device's size.
[[nodiscard]] Result<std::uint64_t> byteOffsetOf(const BlockDevice& device, std::uint64_t lba);

// The table sector at `lba`. A short read is kOutOfRange at the offset it
// started from: half a partition table is not a partition table.
[[nodiscard]] Result<TableSector> readTableSector(BlockDevice& device, std::uint64_t lba);

} // namespace revenant::volume

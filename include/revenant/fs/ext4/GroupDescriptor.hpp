// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Result.hpp"

namespace revenant::fs::ext4 {

// The smallest a group descriptor may be, and the size every volume without the
// 64-bit feature uses. A 64-bit volume's descriptors are wider — how much wider
// is the superblock's `s_desc_size` to say — and carry the high halves of the
// block numbers the extra room is for.
inline constexpr std::size_t kSmallDescriptorBytes = 32;
inline constexpr std::size_t kWideDescriptorBytes = 64;

// The one thing a walk needs from a block group's descriptor: where that
// group's inode table starts. The free counts and checksums beside it are
// bookkeeping for a *writer*, and this build never writes (ADR-0005).
struct Ext4Group {
	std::uint64_t inodeTableBlock;
};

// Reads one group descriptor of `descriptorBytes` bytes.
//
// A descriptor size below 32 is `kInvalidArgument` — no ext4 volume has one, and
// believing it would mean reading a block number out of a field that is not
// there. Input shorter than the descriptor is `kOutOfRange`.
[[nodiscard]] Result<Ext4Group>
parseGroupDescriptor(std::span<const std::byte> slot, std::size_t descriptorBytes);

} // namespace revenant::fs::ext4

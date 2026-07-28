// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. exFAT's allocation bitmap: one bit per cluster, saying whether the
// volume considers it in use. Not a public interface.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fs/ClusterChain.hpp"

namespace revenant::fs::exfat {

// A bitmap larger than this is not a bitmap, it is a volume built to exhaust
// whoever reads it (ADR-0009). 64 MiB of bits covers 512 million clusters.
inline constexpr std::size_t kMaxBitmapBytes = 64U << 20U;

// What the volume says about its own clusters — or nothing, when the bitmap
// entry was absent or unreadable.
//
// The question it answers is the one that decides whether a *deleted* file's
// bytes are still its own: a cluster the volume has since handed to something
// else no longer holds what the directory entry claims, and handing those bytes
// back would be worse than handing back none.
class AllocationBitmap {
public:
	AllocationBitmap() = default;
	explicit AllocationBitmap(std::vector<std::byte> bits) noexcept;

	// False when there is no bitmap to consult, in which case nothing may be
	// concluded from it either way.
	[[nodiscard]] bool known() const noexcept;

	// Whether `cluster` is marked in use. A cluster the bitmap does not cover
	// reads as allocated: the safe answer, since the alternative is to hand
	// back bytes on the strength of a bitmap that never described them.
	[[nodiscard]] bool isAllocated(std::uint32_t cluster) const noexcept;

private:
	std::vector<std::byte> bits_;
};

// Reads the bitmap a directory's own entries point at, or an unknown bitmap
// when there is none among them.
[[nodiscard]] AllocationBitmap
readAllocationBitmap(const ClusterChain& chain, std::span<const std::byte> directoryBytes);

} // namespace revenant::fs::exfat

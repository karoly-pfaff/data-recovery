// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. A borrowed device read at byte offsets, with the one rule every
// filesystem here applies to it: a short read is a failure, not a shorter
// answer. Read-only throughout (ADR-0005); the device is borrowed, never owned,
// and must outlive this. Not a public interface.

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::fs {

class VolumeReader {
public:
	explicit VolumeReader(BlockDevice& device) noexcept;

	// Fills `buffer` from `offset`. A short read is kOutOfRange: the volume ends
	// inside what was asked for, which makes it unreadable rather than empty, and
	// a parser handed a half-filled buffer would read the tail as zeros.
	[[nodiscard]] Result<std::size_t> read(std::uint64_t offset, std::span<std::byte> buffer) const;

private:
	BlockDevice* device_; // non-owning, never null
};

} // namespace revenant::fs

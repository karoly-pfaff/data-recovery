// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Result.hpp"

namespace revenant {

// The single seam through which every byte source is read: random-access,
// read-only, byte-addressed (ADR-0005). No write operation exists here —
// the source device can never be modified through this interface.
class BlockDevice {
public:
	virtual ~BlockDevice() = default;
	BlockDevice() = default;
	BlockDevice(const BlockDevice&) = delete;
	BlockDevice& operator=(const BlockDevice&) = delete;
	BlockDevice(BlockDevice&&) = delete;
	BlockDevice& operator=(BlockDevice&&) = delete;

	// Total addressable size in bytes.
	[[nodiscard]] virtual std::uint64_t sizeInBytes() const = 0;

	// Native sector size (512 or 4096). Reads need not be sector-aligned.
	[[nodiscard]] virtual std::uint32_t sectorSize() const = 0;

	// Reads up to buffer.size() bytes starting at `offset`; returns the count
	// actually read. A short read at end-of-device is a value, not an error;
	// a device fault is a typed error.
	[[nodiscard]] virtual Result<std::size_t>
	readAt(std::uint64_t offset, std::span<std::byte> buffer) = 0;
};

} // namespace revenant

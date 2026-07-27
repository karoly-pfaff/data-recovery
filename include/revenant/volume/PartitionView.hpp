// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::volume {

// A byte-range window over another BlockDevice: the "partition" seam the fs
// layer mounts, without MBR/GPT parsing (that is M4). Read-only by nature.
class PartitionView final : public BlockDevice {
public:
	// `length` is clamped to the parent's remaining size; `start` past the
	// parent's end yields a zero-length view (values, not errors).
	PartitionView(BlockDevice& parent, std::uint64_t start, std::uint64_t length) noexcept;

	[[nodiscard]] std::uint64_t sizeInBytes() const override;
	[[nodiscard]] std::uint32_t sectorSize() const override;
	[[nodiscard]] Result<std::size_t>
	readAt(std::uint64_t offset, std::span<std::byte> buffer) override;

private:
	BlockDevice& parent_;
	std::uint64_t start_;
	std::uint64_t sizeInBytes_;
};

} // namespace revenant::volume

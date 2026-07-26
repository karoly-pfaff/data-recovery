// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::testing {

// Deterministic, privilege-free BlockDevice over an owned buffer — the byte
// source backing most unit tests (test-only by design; see story-0001).
class InMemoryDevice final : public BlockDevice {
public:
	InMemoryDevice(std::vector<std::byte> data, std::uint32_t sectorSize);

	[[nodiscard]] std::uint64_t sizeInBytes() const override;
	[[nodiscard]] std::uint32_t sectorSize() const override;
	[[nodiscard]] Result<std::size_t>
	readAt(std::uint64_t offset, std::span<std::byte> buffer) override;

private:
	std::vector<std::byte> data_;
	std::uint32_t sectorSize_;
};

} // namespace revenant::testing

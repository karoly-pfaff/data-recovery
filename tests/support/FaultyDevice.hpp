// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::testing {

// A range of the device that will not read.
struct Fault {
	std::uint64_t offsetBytes = 0;
	std::uint64_t lengthBytes = 0;
	// How many reads a *transient* fault refuses before it clears. A permanent
	// one refuses every read whatever this says. The transient case is how a
	// retry can be shown to have *worked* rather than merely been attempted.
	unsigned refusals = 0;
	bool permanent = true;
};

// A BlockDevice over an owned buffer that refuses reads overlapping its faults.
// Counts its reads, so a cache can be shown to have prevented one.
class FaultyDevice final : public BlockDevice {
public:
	FaultyDevice(std::vector<std::byte> data, std::uint32_t sectorSize, std::vector<Fault> faults);

	[[nodiscard]] std::uint64_t sizeInBytes() const override;
	[[nodiscard]] std::uint32_t sectorSize() const override;
	[[nodiscard]] Result<std::size_t>
	readAt(std::uint64_t offset, std::span<std::byte> buffer) override;

	[[nodiscard]] std::uint64_t reads() const noexcept {
		return reads_;
	}

private:
	// The fault this read runs into, or null. Non-owning; valid while this
	// device is.
	[[nodiscard]] Fault* faultFor(std::uint64_t offset, std::size_t length);
	[[nodiscard]] std::size_t availableAt(std::uint64_t offset, std::size_t wanted) const;
	[[nodiscard]] std::size_t
	copyOut(std::uint64_t offset, std::span<std::byte> buffer, std::size_t count);

	std::vector<std::byte> data_;
	std::uint32_t sectorSize_;
	std::vector<Fault> faults_;
	std::uint64_t reads_ = 0;
};

} // namespace revenant::testing

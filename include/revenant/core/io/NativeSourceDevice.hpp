// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant {

// A `BlockDevice` whose bytes come from an OS handle, which it owns and closes.
// It answers the two questions the handle already knows the answers to, and
// leaves `readAt` to whoever derives from it — because that is the one thing an
// image file and a raw device do differently: a file will read any range, and a
// device will only read whole sectors.
//
// Not a seam. `BlockDevice` is the seam; this is the shared *implementation* of
// "a handle, a size and a sector size", written once so the second device backed
// by one is not the first one copied.
class NativeSourceDevice : public BlockDevice {
public:
	~NativeSourceDevice() override;
	// BlockDevice already deletes copy/move; restated here (rule of five)
	// because this class declares its own destructor.
	NativeSourceDevice(const NativeSourceDevice&) = delete;
	NativeSourceDevice& operator=(const NativeSourceDevice&) = delete;
	NativeSourceDevice(NativeSourceDevice&&) = delete;
	NativeSourceDevice& operator=(NativeSourceDevice&&) = delete;

	[[nodiscard]] std::uint64_t sizeInBytes() const override {
		return sizeInBytes_;
	}

	[[nodiscard]] std::uint32_t sectorSize() const override {
		return sectorSize_;
	}

protected:
	// NOLINTBEGIN(bugprone-easily-swappable-parameters) - three adjacent,
	// mutually-convertible integral types, but every caller is a derived
	// device's own constructor, itself reachable only through that device's
	// open().
	NativeSourceDevice(
		std::intptr_t nativeHandle,
		std::uint64_t sizeBytes,
		std::uint32_t sectorSize) noexcept
		: nativeHandle_(nativeHandle), sizeInBytes_(sizeBytes), sectorSize_(sectorSize) {}

	// NOLINTEND(bugprone-easily-swappable-parameters)

	// A positioned read of exactly what it is given. Whether that range had to be
	// clamped or aligned first is the deriving device's business.
	[[nodiscard]] Result<std::size_t>
	readHandle(std::uint64_t offset, std::span<std::byte> buffer) const;

private:
	std::intptr_t nativeHandle_;
	std::uint64_t sizeInBytes_;
	std::uint32_t sectorSize_;
};

} // namespace revenant

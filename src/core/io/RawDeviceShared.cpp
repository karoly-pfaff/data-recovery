// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

#include "core/io/RawDeviceNative.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/AlignedRead.hpp"
#include "revenant/core/io/RawDevice.hpp"
#include "revenant/core/io/ReadRange.hpp"

// The two RawDevice member-function bodies that need no platform-specific logic
// once the platform provides acquireRawDevice. Closing and reading the handle
// belong to NativeSource, which this shares with ImageFileDevice; the one thing
// this device adds is the alignment, and that is a header both platforms compile.

namespace revenant {

// The aligned window never runs past the end of the device: a block device's
// size is a whole number of sectors, so rounding a range already clamped to it
// up to a sector boundary lands exactly on that end.
Result<std::size_t> RawDevice::readAt(std::uint64_t offset, std::span<std::byte> buffer) {
	const auto want = clampReadRange(offset, buffer.size(), sizeInBytes());
	if (!want.hasValue()) {
		return want.error();
	}
	return readThroughAlignment(
		offset,
		buffer.first(want.value()),
		sectorSize(),
		[this](std::uint64_t at, std::span<std::byte> into) { return readHandle(at, into); });
}

Result<std::unique_ptr<RawDevice>> RawDevice::open(const std::filesystem::path& devicePath) {
	const auto opened = acquireRawDevice(devicePath);
	if (!opened.hasValue()) {
		return opened.error();
	}
	return std::make_unique<RawDevice>(
		RawDevice::ConstructTag{},
		opened.value().nativeHandle,
		opened.value().sizeInBytes,
		opened.value().sectorSize);
}

} // namespace revenant

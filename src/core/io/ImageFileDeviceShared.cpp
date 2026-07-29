// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/ImageFileDevice.hpp"
#include "revenant/core/io/ReadRange.hpp"

// The two ImageFileDevice member-function bodies that need no platform-specific
// logic. Opening, closing, reading and measuring are the platform's four
// primitives (declared in ReadRange.hpp, defined in NativeIoPosix.cpp /
// NativeIoWindows.cpp); an image is nothing but the first and the last of them
// put together, which is why that pairing lives here rather than twice.

namespace revenant {

namespace {

// The two primitives an image needs, put together: a handle and how far it goes.
[[nodiscard]] Result<std::unique_ptr<ImageFileDevice>>
deviceOver(std::intptr_t handle, std::uint32_t sectorSize) {
	return queryFileSize(handle).map([handle, sectorSize](std::uint64_t size) {
		return std::make_unique<ImageFileDevice>(
			ImageFileDevice::ConstructTag{},
			handle,
			size,
			sectorSize);
	});
}

} // namespace

Result<std::size_t> ImageFileDevice::readAt(std::uint64_t offset, std::span<std::byte> buffer) {
	const auto want = clampReadRange(offset, buffer.size(), sizeInBytes());
	if (!want.hasValue()) {
		return want.error();
	}
	return readHandle(offset, buffer.first(want.value()));
}

Result<std::unique_ptr<ImageFileDevice>>
ImageFileDevice::open(const std::filesystem::path& imagePath, std::uint32_t sectorSize) {
	if (sectorSize == 0) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	return openReadOnly(imagePath).andThen(
		[sectorSize](std::intptr_t handle) { return deviceOver(handle, sectorSize); });
}

} // namespace revenant

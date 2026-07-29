// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/NativeSourceDevice.hpp"

namespace revenant {

inline constexpr std::uint32_t kDefaultSectorSize = 512;

// Read-only BlockDevice over a raw image file (.dd/.img), the portable,
// privilege-free source used throughout development. Positioned reads only —
// no shared mutable file offset, so concurrent readAt calls are safe.
//
// An image's sector size is the caller's choice rather than the file's own
// property, and a file will read any range it is asked for. That is the whole
// difference between this and `RawDevice` (story-0401); everything else they
// need is in the base they share.
//
// Platform I/O lives in NativeIoPosix.cpp / NativeIoWindows.cpp, selected by
// CMake (no #ifdef in shared code).
class ImageFileDevice final : public NativeSourceDevice {
public:
	// Opens `imagePath` read-only. Missing/unreadable path -> kNotFound; refused
	// for want of privilege -> kPermissionDenied; sectorSize == 0 ->
	// kInvalidArgument.
	[[nodiscard]] static Result<std::unique_ptr<ImageFileDevice>>
	open(const std::filesystem::path& imagePath, std::uint32_t sectorSize = kDefaultSectorSize);

	[[nodiscard]] Result<std::size_t>
	readAt(std::uint64_t offset, std::span<std::byte> buffer) override;

	// Construction goes through open(); the tag blocks direct outside use while
	// keeping the constructor public for std::make_unique.
	struct ConstructTag {};

	ImageFileDevice(
		ConstructTag /*unused*/,
		std::intptr_t nativeHandle,
		std::uint64_t sizeBytes,
		std::uint32_t sectorSize) noexcept
		: NativeSourceDevice(nativeHandle, sizeBytes, sectorSize) {}
};

} // namespace revenant

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

// Read-only BlockDevice over a raw device: a whole disk (`\\.\PhysicalDrive0`,
// `/dev/sda`) or one of its volumes (`\\.\C:`, `/dev/sda1`).
//
// One class rather than the two the I/O layer once named, because they are the
// same object: on both platforms a disk and a volume are opened by the same call
// and measured by the same query, and differ only in the path an operator types.
//
// Reads need not be aligned — `BlockDevice` promises they need not, and the
// parsers above take that promise — even though the OS will only issue aligned
// ones. That is the one thing this device does which `ImageFileDevice` does not,
// and the reconciliation is in `AlignedRead.hpp`.
//
// Platform I/O lives in NativeIoPosix.cpp / NativeIoWindows.cpp; the measuring
// that is a device's alone is in RawDevicePosix.cpp / RawDeviceWindows.cpp.
class RawDevice final : public NativeSourceDevice {
public:
	// Opens `devicePath` read-only. No such device -> kNotFound; a refusal for
	// want of privilege -> kPermissionDenied, which is by far the likeliest thing
	// to go wrong the first time an operator points this at a real disk.
	[[nodiscard]] static Result<std::unique_ptr<RawDevice>>
	open(const std::filesystem::path& devicePath);

	[[nodiscard]] Result<std::size_t>
	readAt(std::uint64_t offset, std::span<std::byte> buffer) override;

	// Construction goes through open(); the tag blocks direct outside use while
	// keeping the constructor public for std::make_unique.
	struct ConstructTag {};

	RawDevice(
		ConstructTag /*unused*/,
		std::intptr_t nativeHandle,
		std::uint64_t sizeBytes,
		std::uint32_t sectorSize) noexcept
		: NativeSourceDevice(nativeHandle, sizeBytes, sectorSize) {}
};

} // namespace revenant

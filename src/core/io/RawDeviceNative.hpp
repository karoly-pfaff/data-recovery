// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The one thing a raw device needs from the OS that an image file does
// not — opening and measuring it — declared once and defined per-platform in
// RawDevicePosix.cpp / RawDeviceWindows.cpp. Everything else it does lives in
// AlignedRead.hpp, where both platforms compile it and a test can drive it
// without a disk. Not a public interface.

#include <cstdint>
#include <filesystem>

#include "revenant/core/Result.hpp"

namespace revenant {

// What a device turned out to be, once the OS was asked.
struct OpenedRawDevice {
	std::intptr_t nativeHandle = 0;
	std::uint64_t sizeInBytes = 0;
	std::uint32_t sectorSize = 0;
};

// Opens the device read-only and measures it. No such device -> kNotFound;
// refused for want of privilege -> kPermissionDenied.
//
// This is the *only* thing a raw device needs that an image file does not.
// Closing the handle and reading through it are the same two calls either way
// (`closeNative`/`readNative`, declared in ReadRange.hpp), and both devices get
// them from the `NativeSource` they hold.
[[nodiscard]] Result<OpenedRawDevice> acquireRawDevice(const std::filesystem::path& devicePath);

} // namespace revenant

// SPDX-License-Identifier: GPL-3.0-or-later
// NOLINTBEGIN(misc-include-cleaner) - windows.h is the single correct include
// for the Win32 API surface below, and winioctl.h for the disk IOCTLs. Their
// internal constituent headers are not meant to be included standalone, so
// misc-include-cleaner cannot resolve a "direct" header for these symbols.
#include <windows.h>
// winioctl.h must follow windows.h; it has no standalone include guard order of
// its own.
#include <winioctl.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

#include "core/io/RawDeviceNative.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/ReadRange.hpp"

namespace revenant {

namespace {

// What every partition table's own arithmetic assumes, and what a device whose
// geometry will not answer is overwhelmingly likely to be.
constexpr std::uint32_t kAssumedSectorSize = 512;

// One IOCTL that fills `into`. DeviceIoControl's buffers are void*, so the
// caller hands it the bytes of a Win32 struct rather than the struct.
[[nodiscard]] bool queryDevice(HANDLE handle, DWORD code, std::span<std::byte> into) {
	DWORD returned = 0;
	return ::DeviceIoControl(
			   handle,
			   code,
			   nullptr,
			   0,
			   into.data(),
			   static_cast<DWORD>(into.size()),
			   &returned,
			   nullptr) != 0;
}

[[nodiscard]] Result<std::uint64_t> queryDeviceSize(HANDLE handle) {
	GET_LENGTH_INFORMATION length{};
	if (!queryDevice(
			handle,
			IOCTL_DISK_GET_LENGTH_INFO,
			std::as_writable_bytes(std::span{&length, 1}))) {
		const auto failure = static_cast<std::int32_t>(::GetLastError());
		::CloseHandle(handle);
		return Error{.code = ErrorCode::kIoFailure, .offset = 0, .osCode = failure};
	}
	return static_cast<std::uint64_t>(length.Length.QuadPart);
}

// A device whose geometry will not answer falls back to 512 rather than failing
// the open: a wrong-but-conventional sector size still reads, and refusing would
// lose a device that is otherwise perfectly readable.
[[nodiscard]] std::uint32_t queryLogicalSectorSize(HANDLE handle) {
	DISK_GEOMETRY_EX geometry{};
	if (!queryDevice(
			handle,
			IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
			std::as_writable_bytes(std::span{&geometry, 1}))) {
		return kAssumedSectorSize;
	}
	const DWORD stated = geometry.Geometry.BytesPerSector;
	return stated == 0 ? kAssumedSectorSize : static_cast<std::uint32_t>(stated);
}

[[nodiscard]] Result<OpenedRawDevice> measure(std::intptr_t nativeHandle) {
	auto* handle = std::bit_cast<HANDLE>(nativeHandle);
	return queryDeviceSize(handle).map([handle, nativeHandle](std::uint64_t bytes) {
		return OpenedRawDevice{
			.nativeHandle = nativeHandle,
			.sizeInBytes = bytes,
			.sectorSize = queryLogicalSectorSize(handle)};
	});
}

} // namespace

Result<OpenedRawDevice> acquireRawDevice(const std::filesystem::path& devicePath) {
	return openReadOnly(devicePath).andThen(measure);
}

} // namespace revenant

// NOLINTEND(misc-include-cleaner)

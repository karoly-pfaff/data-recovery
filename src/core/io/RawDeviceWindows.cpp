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
#include "core/io/WindowsDeviceQuery.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/ReadRange.hpp"

namespace revenant {

namespace {

// What every partition table's own arithmetic assumes, and what a device whose
// geometry will not answer is overwhelmingly likely to be.
constexpr std::uint32_t kAssumedSectorSize = 512;

// The two requests, restated in the type the shared query takes them in.
constexpr std::uint32_t kLengthRequest = IOCTL_DISK_GET_LENGTH_INFO;
constexpr std::uint32_t kGeometryRequest = IOCTL_DISK_GET_DRIVE_GEOMETRY_EX;

[[nodiscard]] Result<std::uint64_t> queryDeviceSize(std::intptr_t handle) {
	GET_LENGTH_INFORMATION length{};
	if (!queryDevice(handle, kLengthRequest, std::as_writable_bytes(std::span{&length, 1}))) {
		const auto failure = static_cast<std::int32_t>(::GetLastError());
		::CloseHandle(std::bit_cast<HANDLE>(handle));
		return Error{.code = ErrorCode::kIoFailure, .offset = 0, .osCode = failure};
	}
	return static_cast<std::uint64_t>(length.Length.QuadPart);
}

// A device whose geometry will not answer falls back to 512 rather than failing
// the open: a wrong-but-conventional sector size still reads, and refusing would
// lose a device that is otherwise perfectly readable.
[[nodiscard]] std::uint32_t queryLogicalSectorSize(std::intptr_t handle) {
	DISK_GEOMETRY_EX geometry{};
	if (!queryDevice(handle, kGeometryRequest, std::as_writable_bytes(std::span{&geometry, 1}))) {
		return kAssumedSectorSize;
	}
	const DWORD stated = geometry.Geometry.BytesPerSector;
	return stated == 0 ? kAssumedSectorSize : static_cast<std::uint32_t>(stated);
}

[[nodiscard]] Result<OpenedRawDevice> measure(std::intptr_t nativeHandle) {
	return queryDeviceSize(nativeHandle).map([nativeHandle](std::uint64_t bytes) {
		return OpenedRawDevice{
			.nativeHandle = nativeHandle,
			.sizeInBytes = bytes,
			.sectorSize = queryLogicalSectorSize(nativeHandle)};
	});
}

} // namespace

Result<OpenedRawDevice> acquireRawDevice(const std::filesystem::path& devicePath) {
	return openReadOnly(devicePath).andThen(measure);
}

} // namespace revenant

// NOLINTEND(misc-include-cleaner)

// SPDX-License-Identifier: GPL-3.0-or-later
// NOLINTBEGIN(misc-include-cleaner) - windows.h is the single correct include
// for the Win32 API surface below, and winioctl.h for the volume IOCTLs. Their
// internal constituent headers are not meant to be included standalone, so
// misc-include-cleaner cannot resolve a "direct" header for these symbols.
#include <windows.h>
// winioctl.h must follow windows.h; it has no standalone include guard order of
// its own.
#include <winioctl.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "core/io/WindowsDeviceQuery.hpp"
#include "core/io/WindowsVolumeExtents.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/DeviceIdentity.hpp"

namespace revenant {

namespace {

// The two requests, restated in the type the shared query takes them in.
constexpr std::uint32_t kVolumeExtentsRequest = IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS;
constexpr std::uint32_t kDeviceNumberRequest = IOCTL_STORAGE_GET_DEVICE_NUMBER;

// How many extents a spanned volume is asked for. A volume with more makes the
// query fail rather than answer with a prefix — a truncated extent list is the
// silent "no overlap" this check exists to remove.
constexpr std::size_t kMaxExtents = 64;

// The whole reply: a count, then that many extents laid out end to end.
constexpr std::size_t kReplyBytes =
	offsetof(VOLUME_DISK_EXTENTS, Extents) + (kMaxExtents * sizeof(DISK_EXTENT));

// A handle opened to ask questions rather than to read: zero desired access
// answers every request below and grants nothing at all. ADR-0005 — no path
// through this file can become one that writes.
class QueryHandle {
public:
	explicit QueryHandle(const std::wstring& devicePath)
		: handle_(
			  ::CreateFileW(
				  devicePath.c_str(),
				  0,
				  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				  nullptr,
				  OPEN_EXISTING,
				  0,
				  nullptr)) {}

	~QueryHandle() {
		if (valid()) {
			::CloseHandle(handle_);
		}
	}

	QueryHandle(const QueryHandle&) = delete;
	QueryHandle& operator=(const QueryHandle&) = delete;
	QueryHandle(QueryHandle&&) = delete;
	QueryHandle& operator=(QueryHandle&&) = delete;

	[[nodiscard]] bool valid() const noexcept {
		return handle_ != INVALID_HANDLE_VALUE;
	}

	[[nodiscard]] std::intptr_t native() const noexcept {
		return std::bit_cast<std::intptr_t>(handle_);
	}

private:
	HANDLE handle_;
};

// One extent of the reply, copied out rather than reinterpreted in place: the
// buffer is bytes off a device, and `DISK_EXTENT` has alignment the bytes do
// not promise.
[[nodiscard]] StorageExtent extentAt(std::span<const std::byte> reply, std::size_t index) {
	const std::size_t at = offsetof(VOLUME_DISK_EXTENTS, Extents) + (index * sizeof(DISK_EXTENT));
	DISK_EXTENT extent{};
	std::memcpy(&extent, reply.subspan(at, sizeof(extent)).data(), sizeof(extent));
	return StorageExtent{
		.disk = extent.DiskNumber,
		.offsetBytes = static_cast<std::uint64_t>(extent.StartingOffset.QuadPart),
		.lengthBytes = static_cast<std::uint64_t>(extent.ExtentLength.QuadPart)};
}

// The count comes off the device, so it bounds nothing by itself: a driver that
// reported more than the buffer was sized for would walk the loop below past
// the end of it. Refused rather than truncated, because a short extent list
// understates where a volume is, and understating that is how a destination on
// the source gets allowed.
[[nodiscard]] Result<DWORD> extentCountIn(std::span<const std::byte> reply) {
	DWORD counted = 0;
	std::memcpy(&counted, reply.data(), sizeof(counted));
	if (counted > kMaxExtents) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	return counted;
}

[[nodiscard]] Result<StorageExtents> extentsIn(std::span<const std::byte> reply) {
	return extentCountIn(reply).map([reply](DWORD counted) {
		StorageExtents storage;
		for (std::size_t index = 0; index < counted; ++index) {
			storage.push_back(extentAt(reply, index));
		}
		return storage;
	});
}

[[nodiscard]] Result<StorageExtents> volumeExtents(std::intptr_t handle) {
	std::vector<std::byte> reply(kReplyBytes);
	if (!queryDevice(handle, kVolumeExtentsRequest, reply)) {
		return lastWin32Failure();
	}
	return extentsIn(reply);
}

// `PartitionNumber` is zero for `\\.\PhysicalDriveN` and the 1-based partition
// for `\\.\C:`, which is the distinction the two answers turn on.
[[nodiscard]] Result<StorageExtents> deviceStorage(std::intptr_t handle) {
	STORAGE_DEVICE_NUMBER number{};
	if (!queryDevice(handle, kDeviceNumberRequest, std::as_writable_bytes(std::span{&number, 1}))) {
		return lastWin32Failure();
	}
	if (number.PartitionNumber != 0) {
		return volumeExtents(handle);
	}
	return StorageExtents{
		StorageExtent{.disk = number.DeviceNumber, .offsetBytes = 0, .lengthBytes = kWholeDisk}};
}

} // namespace

Result<StorageExtents> storageOfVolume(const std::wstring& volumePath) {
	const QueryHandle handle{volumePath};
	if (!handle.valid()) {
		return lastWin32Failure();
	}
	return volumeExtents(handle.native());
}

Result<StorageExtents> storageOfDevicePath(const std::wstring& devicePath) {
	const QueryHandle handle{devicePath};
	if (!handle.valid()) {
		return lastWin32Failure();
	}
	return deviceStorage(handle.native());
}

} // namespace revenant

// NOLINTEND(misc-include-cleaner)

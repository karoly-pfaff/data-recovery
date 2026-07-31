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
#include <cwchar>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "core/io/WindowsDeviceQuery.hpp"
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

[[nodiscard]] Error lastFailure() {
	return Error{
		.code = ErrorCode::kIoFailure,
		.offset = 0,
		.osCode = static_cast<std::int32_t>(::GetLastError())};
}

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

// However many extents the device said it wrote, read back out of the reply.
//
// The count comes off the device, so it bounds nothing by itself: a driver that
// reports more than the buffer was sized for would walk this loop past the end
// of it. Refused rather than truncated, because a short extent list understates
// where a volume is, and understating that is how a destination on the source
// gets allowed.
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

// Which disks, and where on them, a volume's bytes live. One extent for a plain
// partition, several for a spanned volume — so the set is the identity, and
// spanned storage is covered without knowing it exists.
[[nodiscard]] Result<StorageExtents> volumeExtents(std::intptr_t handle) {
	std::vector<std::byte> reply(kReplyBytes);
	if (!queryDevice(handle, kVolumeExtentsRequest, reply)) {
		return lastFailure();
	}
	return extentsIn(reply);
}

// A volume source's own extents; a whole disk's every byte. `PartitionNumber`
// is zero for `\\.\PhysicalDriveN` and the 1-based partition for `\\.\C:`,
// which is the distinction the two rules turn on.
[[nodiscard]] Result<StorageExtents> deviceStorage(std::intptr_t handle) {
	STORAGE_DEVICE_NUMBER number{};
	if (!queryDevice(handle, kDeviceNumberRequest, std::as_writable_bytes(std::span{&number, 1}))) {
		return lastFailure();
	}
	if (number.PartitionNumber != 0) {
		return volumeExtents(handle);
	}
	return StorageExtents{
		StorageExtent{.disk = number.DeviceNumber, .offsetBytes = 0, .lengthBytes = kWholeDisk}};
}

// The mount point a path sits under: `C:\`, or the folder a volume is mounted
// at, or a share root.
[[nodiscard]] Result<std::wstring> mountPointOf(const std::filesystem::path& directory) {
	std::wstring mount(MAX_PATH, L'\0');
	if (::GetVolumePathNameW(directory.c_str(), mount.data(), MAX_PATH) == 0) {
		return lastFailure();
	}
	mount.resize(std::wcslen(mount.c_str()));
	return mount;
}

// The volume GUID path of a mount point, which is openable whether the volume
// wears a drive letter or is mounted at a folder — asking by the letter would
// lose the second kind.
[[nodiscard]] Result<std::wstring> volumeNameOf(const std::wstring& mount) {
	std::wstring name(MAX_PATH, L'\0');
	if (::GetVolumeNameForVolumeMountPointW(mount.c_str(), name.data(), MAX_PATH) == 0) {
		return lastFailure();
	}
	name.resize(std::wcslen(name.c_str()));
	if (name.empty()) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	// `\\?\Volume{…}\` names the volume; `\\?\Volume{…}` opens it.
	name.pop_back();
	return name;
}

[[nodiscard]] Result<StorageExtents> extentsOfVolume(const std::wstring& volumeName) {
	const QueryHandle handle{volumeName};
	if (!handle.valid()) {
		return lastFailure();
	}
	return volumeExtents(handle.native());
}

} // namespace

Result<StorageExtents> storageUnder(const std::filesystem::path& directory) {
	return mountPointOf(directory).andThen([](const std::wstring& mount) -> Result<StorageExtents> {
		// A share sits on no local disk, so it overlaps nothing — which is how
		// ADR-0007's permitted network destination keeps working.
		if (::GetDriveTypeW(mount.c_str()) == DRIVE_REMOTE) {
			return StorageExtents{};
		}
		return volumeNameOf(mount).andThen(extentsOfVolume);
	});
}

Result<StorageExtents> storageOf(const std::filesystem::path& devicePath) {
	const QueryHandle handle{devicePath.wstring()};
	if (!handle.valid()) {
		return lastFailure();
	}
	return deviceStorage(handle.native());
}

} // namespace revenant

// NOLINTEND(misc-include-cleaner)

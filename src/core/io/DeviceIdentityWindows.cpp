// SPDX-License-Identifier: GPL-3.0-or-later
// NOLINTBEGIN(misc-include-cleaner) - windows.h is the single correct include
// for the Win32 API surface below. Its internal constituent headers are not
// meant to be included standalone, so misc-include-cleaner cannot resolve a
// "direct" header for these symbols.
#include <windows.h>

#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <string>

#include "core/io/WindowsVolumeExtents.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/DeviceIdentity.hpp"

namespace revenant {

namespace {

[[nodiscard]] Error lastFailure() {
	return Error{
		.code = ErrorCode::kIoFailure,
		.offset = 0,
		.osCode = static_cast<std::int32_t>(::GetLastError())};
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

} // namespace

Result<StorageExtents> storageUnder(const std::filesystem::path& directory) {
	return mountPointOf(directory).andThen([](const std::wstring& mount) -> Result<StorageExtents> {
		// A share sits on no local disk, so it overlaps nothing — which is how
		// ADR-0007's permitted network destination keeps working.
		if (::GetDriveTypeW(mount.c_str()) == DRIVE_REMOTE) {
			return StorageExtents{};
		}
		return volumeNameOf(mount).andThen(storageOfVolume);
	});
}

Result<StorageExtents> storageOf(const std::filesystem::path& devicePath) {
	return storageOfDevicePath(devicePath.wstring());
}

} // namespace revenant

// NOLINTEND(misc-include-cleaner)

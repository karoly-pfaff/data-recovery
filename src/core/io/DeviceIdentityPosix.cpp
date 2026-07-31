// SPDX-License-Identifier: GPL-3.0-or-later
#include <sys/stat.h>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "core/io/MountTable.hpp"
#include "core/io/SysfsStorage.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/DeviceIdentity.hpp"

namespace revenant {

namespace {

// The kernel's own record of what is mounted where, and from what.
constexpr const char* kMountInfo = "/proc/self/mountinfo";

[[nodiscard]] Result<struct stat> statOf(const std::filesystem::path& path) {
	struct stat status{};
	if (::stat(path.c_str(), &status) != 0) {
		return Error{.code = ErrorCode::kIoFailure, .offset = 0, .osCode = errno};
	}
	return status;
}

// Line by line rather than through a stream iterator: libstdc++'s iterator form
// reports a potential null dereference inside `streambuf` under `-O2
// -Wnull-dereference`, which is a diagnostic only an optimized GCC build sees.
// A `/proc` file states no size to read at once anyway.
[[nodiscard]] std::optional<std::string> contentsOf(const char* file) {
	std::ifstream reading{file};
	if (!reading) {
		return std::nullopt;
	}
	std::string text;
	for (std::string line; std::getline(reading, line);) {
		text += line + "\n";
	}
	return text;
}

// The device a destination's filesystem is mounted from, as a path. Asked of
// the mount table rather than of `stat`, because `st_dev` is the filesystem's
// number and not the storage's: btrfs and overlayfs report one no block device
// owns, and reading that as "no local disk" is how a destination on the disk
// being recovered walks through the check.
[[nodiscard]] std::optional<std::string> backingDeviceOf(const std::filesystem::path& directory) {
	const auto table = contentsOf(kMountInfo);
	return table.has_value() ? mountSourceFor(table.value(), directory) : std::nullopt;
}

} // namespace

Result<StorageExtents> storageUnder(const std::filesystem::path& directory) {
	const auto backing = backingDeviceOf(directory);
	if (!backing.has_value()) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	// A mount whose source is not a block device sits on no local disk and so
	// overlaps nothing — a share, a tmpfs, a pseudo-filesystem. That is a real
	// answer, and it is what keeps ADR-0007's network destination working.
	const auto node = statOf(backing.value());
	if (!node.hasValue() || !S_ISBLK(node.value().st_mode)) {
		return StorageExtents{};
	}
	return storageOfBlockDevice(node.value().st_rdev);
}

Result<StorageExtents> storageOf(const std::filesystem::path& devicePath) {
	return statOf(devicePath).andThen([](const struct stat& status) -> Result<StorageExtents> {
		if (!S_ISBLK(status.st_mode)) {
			return Error{.code = ErrorCode::kIoFailure};
		}
		return storageOfBlockDevice(status.st_rdev);
	});
}

} // namespace revenant

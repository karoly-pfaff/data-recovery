// SPDX-License-Identifier: GPL-3.0-or-later
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "core/io/DeviceNumber.hpp"
#include "core/io/MountTable.hpp"
#include "core/io/SysfsStoragePosix.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/DeviceIdentity.hpp"

namespace revenant {

namespace {

// The kernel's own record of what is mounted where, and from what.
constexpr const char* kMountInfo = "/proc/self/mountinfo";

// A `dev_t` in the one encoding everything here compares by. The platform packs
// the same two halves differently, and handing a raw `dev_t` to a reader of
// "major:minor" text matches nothing at all — quietly, and in the direction
// that allows.
[[nodiscard]] std::uint64_t keyOf(dev_t device) {
	return deviceKey(major(device), minor(device));
}

[[nodiscard]] Result<struct stat> statOf(const std::filesystem::path& path) {
	struct stat status{};
	if (::stat(path.c_str(), &status) != 0) {
		return Error{.code = ErrorCode::kIoFailure, .offset = 0, .osCode = errno};
	}
	return status;
}

// The storage a block-device path covers. Both sides of the rule reach the
// tree through here, so both are keyed the same way.
[[nodiscard]] Result<StorageExtents> storageOfNode(const struct stat& node) {
	if (!S_ISBLK(node.st_mode)) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	return storageOfBlockDevice(node.st_rdev);
}

[[nodiscard]] std::optional<std::string> contentsOf(const char* file) {
	std::ifstream reading{file};
	if (!reading) {
		return std::nullopt;
	}
	// Line by line rather than through a stream iterator: libstdc++'s iterator
	// form reports a potential null dereference inside `streambuf` under `-O2
	// -Wnull-dereference`, a diagnostic only an optimized GCC build sees. A
	// `/proc` file states no size to read at once anyway.
	std::string text;
	for (std::string line; std::getline(reading, line);) {
		text += line + "\n";
	}
	return text;
}

// What a destination's filesystem was mounted from. Asked of the mount table
// rather than of `stat`, because `st_dev` is the filesystem's number and not
// the storage's: btrfs and overlayfs report one no block device owns, and
// reading that as "no local disk" is how a destination on the disk being
// recovered walks through the check.
[[nodiscard]] std::optional<MountSource>
mountOf(const std::filesystem::path& directory, dev_t fsDevice) {
	const auto table = contentsOf(kMountInfo);
	return table.has_value() ? mountSourceFor(table.value(), directory, keyOf(fsDevice))
							 : std::nullopt;
}

// The storage behind one mount. A type known to hold none is a real answer; a
// source that will not resolve to a block device is not an answer at all, and
// refuses rather than being read as "somewhere else". That is what covers
// overlayfs, ZFS, and a container whose `/dev` does not carry the node its
// mount table names.
[[nodiscard]] Result<StorageExtents> storageBehind(const MountSource& mount) {
	if (holdsNoLocalStorage(mount.type)) {
		return StorageExtents{};
	}
	return statOf(mount.source).andThen(storageOfNode);
}

} // namespace

Result<StorageExtents> storageUnder(const std::filesystem::path& directory) {
	return statOf(directory).andThen([&](const struct stat& here) -> Result<StorageExtents> {
		const auto mount = mountOf(directory, here.st_dev);
		if (!mount.has_value()) {
			return Error{.code = ErrorCode::kIoFailure};
		}
		return storageBehind(mount.value());
	});
}

Result<StorageExtents> storageOf(const std::filesystem::path& devicePath) {
	return statOf(devicePath).andThen(storageOfNode);
}

} // namespace revenant

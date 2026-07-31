// SPDX-License-Identifier: GPL-3.0-or-later
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include "core/io/MountTable.hpp"
#include "core/io/SysfsStorage.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/DeviceIdentity.hpp"

namespace revenant {

namespace {

// The kernel's own record of what is mounted where, and from what.
constexpr const char* kMountInfo = "/proc/self/mountinfo";

// Filesystems that occupy no local block storage, so a destination on one
// overlaps nothing and is allowed: what a recovery run writes there cannot land
// on the disk it is reading. The network half is ADR-0007's permitted
// destination; the memory half holds nothing after a reboot, which is the
// operator's problem and not this check's.
//
// An allowlist rather than a denylist on purpose. A filesystem nobody here has
// heard of does not get the benefit of the doubt: it fails to resolve, and an
// unresolved destination refuses the run.
constexpr std::array kStoragelessTypes{
	std::string_view{"nfs"},
	std::string_view{"nfs4"},
	std::string_view{"cifs"},
	std::string_view{"smb3"},
	std::string_view{"smbfs"},
	std::string_view{"afs"},
	std::string_view{"ceph"},
	std::string_view{"9p"},
	std::string_view{"fuse.sshfs"},
	std::string_view{"glusterfs"},
	std::string_view{"tmpfs"},
	std::string_view{"ramfs"}};

[[nodiscard]] bool holdsNoLocalStorage(std::string_view type) {
	return std::ranges::find(kStoragelessTypes, type) != kStoragelessTypes.end();
}

[[nodiscard]] Result<struct stat> statOf(const std::filesystem::path& path) {
	struct stat status{};
	if (::stat(path.c_str(), &status) != 0) {
		return Error{.code = ErrorCode::kIoFailure, .offset = 0, .osCode = errno};
	}
	return status;
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
mountOf(const std::filesystem::path& directory, std::uint64_t fsDevice) {
	const auto table = contentsOf(kMountInfo);
	return table.has_value() ? mountSourceFor(table.value(), directory, fsDevice) : std::nullopt;
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
	return statOf(mount.source).andThen([](const struct stat& node) -> Result<StorageExtents> {
		if (!S_ISBLK(node.st_mode)) {
			return Error{.code = ErrorCode::kIoFailure};
		}
		return storageOfBlockDevice(node.st_rdev);
	});
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
	return statOf(devicePath).andThen([](const struct stat& status) -> Result<StorageExtents> {
		if (!S_ISBLK(status.st_mode)) {
			return Error{.code = ErrorCode::kIoFailure};
		}
		return storageOfBlockDevice(status.st_rdev);
	});
}

} // namespace revenant

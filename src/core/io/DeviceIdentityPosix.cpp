// SPDX-License-Identifier: GPL-3.0-or-later
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#include "core/SafeArith.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/DeviceIdentity.hpp"

namespace revenant {

namespace {

// sysfs states `start` and `size` in 512-byte units whatever the device's own
// sector size is — a kernel ABI, not a property of the disk.
constexpr std::uint64_t kSysfsUnitBytes = 512;

// The kernel's own answer to "what block device is this, and what is it part
// of", which is why the partition-to-disk step needs no guessing from names.
[[nodiscard]] std::filesystem::path sysfsNodeOf(dev_t device) {
	const std::string name = std::to_string(major(device)) + ":" + std::to_string(minor(device));
	return std::filesystem::path{"/sys/dev/block"} / name;
}

[[nodiscard]] std::optional<std::string> firstLineOf(const std::filesystem::path& file) {
	std::ifstream reading{file};
	std::string line;
	if (!std::getline(reading, line)) {
		return std::nullopt;
	}
	return line;
}

[[nodiscard]] std::optional<std::uint64_t> numberIn(const std::string& text) {
	std::uint64_t value = 0;
	const auto* first = text.data();
	const auto parsed = std::from_chars(first, first + text.size(), value);
	if (parsed.ec != std::errc{}) {
		return std::nullopt;
	}
	return value;
}

[[nodiscard]] std::optional<std::uint64_t> numberFrom(const std::filesystem::path& file) {
	const auto text = firstLineOf(file);
	return text.has_value() ? numberIn(text.value()) : std::nullopt;
}

// A sysfs `dev` file holds "major:minor"; back into the dev_t both halves of
// this file compare disks by.
[[nodiscard]] std::optional<std::uint64_t> deviceNumberFrom(const std::filesystem::path& file) {
	const auto text = firstLineOf(file);
	const auto colon = text.has_value() ? text->find(':') : std::string::npos;
	if (colon == std::string::npos) {
		return std::nullopt;
	}
	const auto high = numberIn(text->substr(0, colon));
	const auto low = numberIn(text->substr(colon + 1));
	if (!high.has_value() || !low.has_value()) {
		return std::nullopt;
	}
	return makedev(static_cast<unsigned int>(high.value()), static_cast<unsigned int>(low.value()));
}

[[nodiscard]] Result<std::uint64_t> unitsToBytes(std::optional<std::uint64_t> units) {
	if (!units.has_value()) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	return safeMul64(units.value(), kSysfsUnitBytes, 0);
}

// Where a partition sits on the disk that carries it. `..` from the sysfs node
// crosses the symlink into the device tree, so the parent it reaches is the
// whole disk rather than another entry in `/sys/dev/block`.
[[nodiscard]] Result<StorageExtents> extentOfPartition(const std::filesystem::path& node) {
	const auto disk = deviceNumberFrom(node / ".." / "dev");
	if (!disk.has_value()) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	return unitsToBytes(numberFrom(node / "start")).andThen([&](std::uint64_t startBytes) {
		return unitsToBytes(numberFrom(node / "size")).map([&](std::uint64_t lengthBytes) {
			return StorageExtents{StorageExtent{
				.disk = disk.value(),
				.offsetBytes = startBytes,
				.lengthBytes = lengthBytes}};
		});
	});
}

// A block device is either a partition of a disk or a disk itself; sysfs says
// which by carrying a `partition` file for the first kind only.
[[nodiscard]] Result<StorageExtents> extentsOfBlockDevice(dev_t device) {
	const auto node = sysfsNodeOf(device);
	std::error_code missing;
	if (std::filesystem::exists(node / "partition", missing)) {
		return extentOfPartition(node);
	}
	if (!std::filesystem::exists(node, missing)) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	return StorageExtents{
		StorageExtent{.disk = device, .offsetBytes = 0, .lengthBytes = kWholeDisk}};
}

[[nodiscard]] Result<struct stat> statOf(const std::filesystem::path& path) {
	struct stat status{};
	if (::stat(path.c_str(), &status) != 0) {
		return Error{.code = ErrorCode::kIoFailure, .offset = 0, .osCode = errno};
	}
	return status;
}

} // namespace

Result<StorageExtents> storageUnder(const std::filesystem::path& directory) {
	return statOf(directory).andThen([](const struct stat& status) -> Result<StorageExtents> {
		// A filesystem the kernel does not back with a block device — a share,
		// a tmpfs — has no sysfs node and so sits on no local disk. That is a
		// real answer, and it is what keeps ADR-0007's network destination
		// working.
		std::error_code missing;
		if (!std::filesystem::exists(sysfsNodeOf(status.st_dev), missing)) {
			return StorageExtents{};
		}
		return extentsOfBlockDevice(status.st_dev);
	});
}

Result<StorageExtents> storageOf(const std::filesystem::path& devicePath) {
	return statOf(devicePath).andThen([](const struct stat& status) -> Result<StorageExtents> {
		if (!S_ISBLK(status.st_mode)) {
			return Error{.code = ErrorCode::kIoFailure};
		}
		return extentsOfBlockDevice(status.st_rdev);
	});
}

} // namespace revenant

// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/io/SysfsStorage.hpp"

#include <sys/sysmacros.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "core/SafeArith.hpp"
#include "core/TextNumber.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/DeviceIdentity.hpp"

namespace revenant {

namespace {

// sysfs states `start` and `size` in 512-byte units whatever the device's own
// sector size is — a kernel ABI, not a property of the disk.
constexpr std::uint64_t kSysfsUnitBytes = 512;

// How far a stack of mapped devices is followed. LUKS over LVM over a partition
// is three; the bound exists so a cycle cannot become a hang.
constexpr int kMaxStackDepth = 8;

// A device still to be resolved, and how far down the stack it was found.
struct Pending {
	std::uint64_t device;
	int depth;
};

// What one device answered with: its own extents, or the devices it is built
// on, which the caller resolves in turn. Never both.
struct Resolution {
	StorageExtents storage;
	std::vector<Pending> below;
};

[[nodiscard]] std::filesystem::path sysfsNodeOf(std::uint64_t device) {
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

[[nodiscard]] std::optional<std::uint64_t> numberFrom(const std::filesystem::path& file) {
	const auto text = firstLineOf(file);
	return text.has_value() ? numberIn(text.value()) : std::nullopt;
}

// "major:minor", as the number every disk here is keyed by.
[[nodiscard]] std::optional<std::uint64_t> deviceNumberIn(std::string_view text) {
	const auto colon = text.find(':');
	if (colon == std::string_view::npos) {
		return std::nullopt;
	}
	const auto high = numberIn(text.substr(0, colon));
	const auto low = numberIn(text.substr(colon + 1));
	if (!high.has_value() || !low.has_value()) {
		return std::nullopt;
	}
	return makedev(static_cast<unsigned int>(high.value()), static_cast<unsigned int>(low.value()));
}

[[nodiscard]] std::optional<std::uint64_t> deviceNumberFrom(const std::filesystem::path& file) {
	const auto text = firstLineOf(file);
	return text.has_value() ? deviceNumberIn(text.value()) : std::nullopt;
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
[[nodiscard]] Result<Resolution> extentOfPartition(const std::filesystem::path& node) {
	const auto disk = deviceNumberFrom(node / ".." / "dev");
	if (!disk.has_value()) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	return unitsToBytes(numberFrom(node / "start")).andThen([&](std::uint64_t startBytes) {
		return unitsToBytes(numberFrom(node / "size")).map([&](std::uint64_t lengthBytes) {
			return Resolution{
				.storage = {StorageExtent{
					.disk = disk.value(),
					.offsetBytes = startBytes,
					.lengthBytes = lengthBytes}},
				.below = {}};
		});
	});
}

// The devices a mapped or RAID device is built from. One member that cannot be
// named fails the whole answer: a partial union claims less ground than the
// device really covers, and understating that is how a destination on the
// source gets allowed.
[[nodiscard]] std::optional<Error>
appendMember(std::vector<Pending>& below, const std::filesystem::path& member, int depth) {
	const auto number = deviceNumberFrom(member / "dev");
	if (!number.has_value()) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	below.push_back(Pending{.device = number.value(), .depth = depth + 1});
	return std::nullopt;
}

[[nodiscard]] Result<Resolution> membersUnder(const std::filesystem::path& slaves, int depth) {
	std::vector<Pending> below;
	std::error_code failed;
	for (const auto& member : std::filesystem::directory_iterator{slaves, failed}) {
		if (const auto refused = appendMember(below, member.path(), depth)) {
			return refused.value();
		}
	}
	return failed ? Result<Resolution>{Error{.code = ErrorCode::kIoFailure}}
				  : Resolution{.storage = {}, .below = below};
}

[[nodiscard]] bool isPartition(const std::filesystem::path& node) {
	std::error_code missing;
	return std::filesystem::exists(node / "partition", missing);
}

// A mapped or RAID device links the devices it is built from; a plain disk
// links nothing.
[[nodiscard]] bool isStacked(const std::filesystem::path& node) {
	std::error_code missing;
	return !std::filesystem::is_empty(node / "slaves", missing) && !missing;
}

[[nodiscard]] Resolution wholeDiskAt(std::uint64_t device) {
	return Resolution{
		.storage = {StorageExtent{.disk = device, .offsetBytes = 0, .lengthBytes = kWholeDisk}},
		.below = {}};
}

// A block device is a partition of a disk, a device built on other devices, or
// a disk itself; sysfs says which by what it carries beside the node.
[[nodiscard]] Result<Resolution> resolveOne(Pending item) {
	const auto node = sysfsNodeOf(item.device);
	std::error_code missing;
	if (item.depth >= kMaxStackDepth || !std::filesystem::exists(node, missing)) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	if (isPartition(node)) {
		return extentOfPartition(node);
	}
	return isStacked(node) ? membersUnder(node / "slaves", item.depth)
						   : Result<Resolution>{wholeDiskAt(item.device)};
}

// One device off the worklist, its answer folded in. A worklist rather than
// recursion, which this project's lint forbids and which a stack of mapped
// devices does not need.
[[nodiscard]] std::optional<Error> stepOnce(std::vector<Pending>& pending, StorageExtents& into) {
	const auto item = pending.back();
	pending.pop_back();
	const auto step = resolveOne(item);
	if (!step.hasValue()) {
		return step.error();
	}
	into.insert(into.end(), step.value().storage.begin(), step.value().storage.end());
	pending.insert(pending.end(), step.value().below.begin(), step.value().below.end());
	return std::nullopt;
}

} // namespace

Result<StorageExtents> storageOfBlockDevice(std::uint64_t deviceNumber) {
	std::vector<Pending> pending{Pending{.device = deviceNumber, .depth = 0}};
	StorageExtents storage;
	while (!pending.empty()) {
		if (const auto refused = stepOnce(pending, storage)) {
			return refused.value();
		}
	}
	return storage;
}

} // namespace revenant

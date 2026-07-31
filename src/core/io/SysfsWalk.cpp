// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/io/SysfsWalk.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "core/io/DeviceNumber.hpp"
#include "core/io/SysfsFields.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/DeviceIdentity.hpp"

namespace revenant {

namespace {

// How far a stack of devices is followed. LUKS over LVM over a partition is
// three; the bound exists so a tree that points at itself cannot become a hang.
constexpr int kMaxStackDepth = 8;

// A node still to be resolved, named as sysfs names it, and how far down the
// stack it was reached.
struct Pending {
	std::string node;
	int depth;
};

// What one node answered with: its own extents, or more nodes to resolve, or
// both — a partition of a mapped device contributes its window *and* sends the
// device carrying it back round.
struct Resolution {
	StorageExtents storage;
	std::vector<Pending> below;
};

// Whether a node is built on other devices. Unknown is its own answer: a
// `slaves` that exists and will not be read must not be mistaken for one that
// is empty, which is what a plain disk has.
enum class Stacking : std::uint8_t { kPlain, kStacked, kUnknown };

[[nodiscard]] std::optional<std::uint64_t> deviceNumberAt(const std::filesystem::path& file) {
	const auto text = sysfsLine(file);
	return text.has_value() ? deviceKeyIn(text.value()) : std::nullopt;
}

[[nodiscard]] std::optional<std::string> nodeNameAt(const std::filesystem::path& file) {
	const auto text = sysfsLine(file);
	return text.has_value() ? sysfsNodeName(text.value()) : std::nullopt;
}

[[nodiscard]] Stacking stackingOf(const std::filesystem::path& node) {
	std::error_code failed;
	const bool empty = std::filesystem::is_empty(node / "slaves", failed);
	if (failed) {
		return sysfsPresent(node / "slaves") ? Stacking::kUnknown : Stacking::kPlain;
	}
	return empty ? Stacking::kPlain : Stacking::kStacked;
}

// The device carrying a partition, when that device is itself built on others.
// A partition of a mapped or RAID device still sits on whatever that device is
// built from, so the carrier goes back on the worklist. Its members' whole
// extents are a superset of the partition's window — the safe direction.
// An unreadable `slaves` on the carrier refuses, exactly as it does when the
// carrier is asked about directly: "cannot tell whether this sits on other
// devices" is not "it does not", and reading it as the second understates
// where the partition is.
[[nodiscard]] Result<std::vector<Pending>>
carrierBelow(const std::filesystem::path& node, int depth) {
	const auto stacking = stackingOf(node / "..");
	const auto name = nodeNameAt(node / ".." / "dev");
	if (stacking == Stacking::kUnknown) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	if (stacking != Stacking::kStacked || !name.has_value()) {
		return std::vector<Pending>{};
	}
	return std::vector<Pending>{Pending{.node = name.value(), .depth = depth + 1}};
}

// Where a partition sits on the device carrying it. `..` from the node crosses
// the symlink into the device tree, so the parent it reaches is that device
// rather than another entry in the flat index.
[[nodiscard]] Result<StorageExtents>
partitionWindow(const std::filesystem::path& node, std::uint64_t disk) {
	return sysfsUnitsToBytes(sysfsNumber(node / "start")).andThen([&](std::uint64_t startBytes) {
		return sysfsUnitsToBytes(sysfsNumber(node / "size")).map([&](std::uint64_t lengthBytes) {
			return StorageExtents{
				StorageExtent{.disk = disk, .offsetBytes = startBytes, .lengthBytes = lengthBytes}};
		});
	});
}

[[nodiscard]] Result<Resolution> extentOfPartition(const std::filesystem::path& node, int depth) {
	const auto disk = deviceNumberAt(node / ".." / "dev");
	const auto carrier = carrierBelow(node, depth);
	if (!disk.has_value() || !carrier.hasValue()) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	return partitionWindow(node, disk.value()).map([&](const StorageExtents& window) {
		return Resolution{.storage = window, .below = carrier.value()};
	});
}

// One member of a mapped or RAID device. A member that cannot be named fails
// the whole answer: a partial union claims less ground than the device really
// covers, and understating that is how a destination on the source is allowed.
[[nodiscard]] Result<Pending> memberAt(const std::filesystem::path& member, int depth) {
	const auto name = nodeNameAt(member / "dev");
	if (!name.has_value()) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	return Pending{.node = name.value(), .depth = depth + 1};
}

// Every name the directory has, gathered before any of them is read. Stepped by
// hand through the `error_code` overloads: a range-for over a
// `directory_iterator` increments with a form that *throws* when the directory
// stops answering half way, and every other filesystem call here is asked the
// non-throwing way for the same reason.
using Names = std::vector<std::filesystem::path>;

[[nodiscard]] Result<Names> namesUnder(const std::filesystem::path& directory) {
	std::error_code failed;
	Names names;
	auto entry = std::filesystem::directory_iterator{directory, failed};
	for (; !failed && entry != std::filesystem::directory_iterator{}; entry.increment(failed)) {
		names.push_back(entry->path());
	}
	return failed ? Result<Names>{Error{.code = ErrorCode::kIoFailure}} : names;
}

[[nodiscard]] Result<Resolution> membersFrom(const Names& names, int depth) {
	std::vector<Pending> below;
	for (const auto& member : names) {
		const auto pending = memberAt(member, depth);
		if (!pending.hasValue()) {
			return pending.error();
		}
		below.push_back(pending.value());
	}
	return Resolution{.storage = {}, .below = below};
}

[[nodiscard]] Result<Resolution> membersUnder(const std::filesystem::path& slaves, int depth) {
	return namesUnder(slaves).andThen(
		[depth](const Names& names) { return membersFrom(names, depth); });
}

[[nodiscard]] Result<Resolution> wholeDiskAt(const std::filesystem::path& node) {
	const auto disk = deviceNumberAt(node / "dev");
	if (!disk.has_value()) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	return Resolution{
		.storage =
			{StorageExtent{.disk = disk.value(), .offsetBytes = 0, .lengthBytes = kWholeDisk}},
		.below = {}};
}

// Not a partition, so either built on other devices or a disk in its own right
// — and an unreadable `slaves` is neither, which refuses.
[[nodiscard]] Result<Resolution> resolveCarrier(const std::filesystem::path& node, int depth) {
	const auto stacking = stackingOf(node);
	if (stacking == Stacking::kUnknown) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	return stacking == Stacking::kStacked ? membersUnder(node / "slaves", depth)
										  : wholeDiskAt(node);
}

// A node is a partition of a device, a device built on other devices, or a disk
// itself; sysfs says which by what it carries beside the node.
[[nodiscard]] Result<Resolution>
resolveOne(const std::filesystem::path& root, const Pending& item) {
	const auto node = root / item.node;
	if (item.depth >= kMaxStackDepth || !sysfsPresent(node)) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	return sysfsPresent(node / "partition") ? extentOfPartition(node, item.depth)
											: resolveCarrier(node, item.depth);
}

// One node off the worklist, its answer folded in. A worklist rather than
// recursion, which this project's lint forbids and which a stack of mapped
// devices does not need.
[[nodiscard]] std::optional<Error>
stepOnce(const std::filesystem::path& root, std::vector<Pending>& pending, StorageExtents& into) {
	const auto item = pending.back();
	pending.pop_back();
	const auto step = resolveOne(root, item);
	if (!step.hasValue()) {
		return step.error();
	}
	into.insert(into.end(), step.value().storage.begin(), step.value().storage.end());
	pending.insert(pending.end(), step.value().below.begin(), step.value().below.end());
	return std::nullopt;
}

} // namespace

Result<StorageExtents>
storageUnderSysfs(const std::filesystem::path& root, const std::string& nodeName) {
	std::vector<Pending> pending{Pending{.node = nodeName, .depth = 0}};
	StorageExtents storage;
	while (!pending.empty()) {
		if (const auto refused = stepOnce(root, pending, storage)) {
			return refused.value();
		}
	}
	return storage;
}

} // namespace revenant

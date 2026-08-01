// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/fat/DirectoryWalk.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "fs/ClusterChain.hpp"
#include "fs/DirectoryTreeWalk.hpp"
#include "fs/fat/DirectoryBytes.hpp"
#include "fs/fat/EntryFromSlot.hpp"
#include "fs/fat/LongNameAssembly.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/Utf16Name.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/fat/DirectoryEntry.hpp"

namespace revenant::fs::fat {

namespace {

// Where the walk is: which directory, under what path, and whether anything
// above it was deleted.
struct Cursor {
	std::string path;
	std::uint32_t cluster;
	unsigned depth;
	bool underDeleted;
};

// What the walk was told about the volume, as opposed to what it follows
// chains with.
struct WalkOrigin {
	std::uint32_t rootCluster;
	bool nonConforming;
};

// What one slot is visited with: the directory it sits in, and the fragments
// collected ahead of it.
struct SlotContext {
	const Cursor* cursor;
	LongNameBuilder* names;
};

// The long name the fragments spelled, or the 8.3 name when there was none to
// assemble or the ones collected contradicted each other.
[[nodiscard]] DecodedName nameOf(const LongNameBuilder& names, const ShortEntry& entry) {
	return names.assembled().value_or(entry.name);
}

// The tree, walked from an explicit worklist rather than by recursion. A FAT
// directory points at its children, its parent and itself, and every one of
// those numbers came off the disk — so the walk keeps its own stack, visits
// each directory cluster once, and cannot be driven off the C++ one.
class Walk {
public:
	Walk(const ClusterChain& table, const WalkOrigin& origin, EntryVisitor& visitor) noexcept
		: table_(&table), visitor_(&visitor), origin_(origin) {}

	[[nodiscard]] Result<EnumerationStats> run() {
		start();
		return driveWorklist(pending_, [this](const Cursor& cursor) { return walkOne(cursor); })
			.map([this](std::uint64_t reported) { return statsOf(reported); });
	}

private:
	void start() {
		const auto root = origin_.rootCluster;
		visited_.push_back(root);
		pending_.push_back(Cursor{.path = {}, .cluster = root, .depth = 0, .underDeleted = false});
	}

	[[nodiscard]] EnumerationStats statsOf(std::uint64_t reported) const {
		return EnumerationStats{
			.recordsScanned = scanned_,
			.entriesReported = reported,
			.nonConformingVolume = origin_.nonConforming};
	}

	[[nodiscard]] Result<std::uint64_t> walkOne(const Cursor& cursor) {
		return walkOneDirectory(
			[&] { return readDirectory(*table_, cursor.cluster, cursor.underDeleted); },
			[&](std::span<const std::byte> bytes) { return walkSlots(cursor, bytes); });
	}

	[[nodiscard]] std::uint64_t walkSlots(const Cursor& cursor, std::span<const std::byte> bytes) {
		LongNameBuilder names;
		const SlotContext context{.cursor = &cursor, .names = &names};
		return foldSlots(
			upToEndOfDirectory(bytes),
			kDirectoryEntryBytes,
			[&](std::span<const std::byte> slot) { return visitSlot(context, slot); });
	}

	[[nodiscard]] std::uint64_t
	visitSlot(const SlotContext& context, std::span<const std::byte> slot) {
		++scanned_;
		const auto kind = classifyEntry(slot);
		if (!kind.hasValue()) {
			return 0;
		}
		return dispatch(context, slot, kind.value());
	}

	// The fragments belong to *this* short entry, so they are cleared after it
	// has had them and not before — whatever it turned out to be.
	[[nodiscard]] std::uint64_t
	dispatch(const SlotContext& context, std::span<const std::byte> slot, EntryKind kind) {
		if (kind == EntryKind::kLongName) {
			collectFragment(context, slot);
			return 0;
		}
		const auto reported =
			kind == EntryKind::kVolumeLabel ? 0U : shortEntry(context, slot, kind);
		context.names->reset();
		return reported;
	}

	static void collectFragment(const SlotContext& context, std::span<const std::byte> slot) {
		const auto fragment = parseLongNameFragment(slot);
		if (fragment.hasValue()) {
			context.names->add(fragment.value());
		}
	}

	[[nodiscard]] std::uint64_t
	shortEntry(const SlotContext& context, std::span<const std::byte> slot, EntryKind kind) {
		const auto entry = parseShortEntry(slot);
		if (!entry.hasValue()) {
			return 0;
		}
		const auto place = placeOf(context, entry.value());
		if (kind == EntryKind::kDirectory) {
			enqueue(*context.cursor, entry.value(), place);
			return 0;
		}
		return emit(entry.value(), place);
	}

	[[nodiscard]] static EntryPlace placeOf(const SlotContext& context, const ShortEntry& entry) {
		auto name = nameOf(*context.names, entry);
		auto path = joinedPath(context.cursor->path, name.utf8);
		return EntryPlace{
			.path = std::move(path),
			.name = std::move(name),
			.underDeleted = context.cursor->underDeleted};
	}

	// A directory is never an entry: it has no content to hand back. `.` and
	// `..` therefore report nothing on their own, and visiting each directory
	// cluster once is what stops them — and any crafted cycle — from walking in
	// circles.
	void enqueue(const Cursor& cursor, const ShortEntry& entry, const EntryPlace& place) {
		if (!walkable(cursor, entry)) {
			return;
		}
		visited_.push_back(entry.firstCluster);
		pending_.push_back(
			Cursor{
				.path = place.path,
				.cluster = entry.firstCluster,
				.depth = cursor.depth + 1,
				.underDeleted = cursor.underDeleted || entry.deleted});
	}

	[[nodiscard]] bool walkable(const Cursor& cursor, const ShortEntry& entry) const {
		return cursor.depth < kMaxDirectoryDepth && table_->isDataCluster(entry.firstCluster) &&
			   std::ranges::find(visited_, entry.firstCluster) == visited_.end();
	}

	[[nodiscard]] std::uint64_t emit(const ShortEntry& entry, const EntryPlace& place) {
		if (place.name.utf8.empty()) {
			return 0;
		}
		visitor_->onEntry(entryFromSlot(*table_, entry, place));
		return 1;
	}

	const ClusterChain* table_; // non-owning, never null
	EntryVisitor* visitor_;     // non-owning, never null
	std::vector<Cursor> pending_;
	std::vector<std::uint32_t> visited_;
	std::uint64_t scanned_ = 0;
	WalkOrigin origin_;
};

} // namespace

Result<EnumerationStats> walkVolume(
	const ClusterChain& table,
	std::uint32_t rootCluster,
	bool nonConforming,
	EntryVisitor& visitor) {
	const WalkOrigin origin{.rootCluster = rootCluster, .nonConforming = nonConforming};
	Walk walk{table, origin, visitor};
	return walk.run();
}

} // namespace revenant::fs::fat

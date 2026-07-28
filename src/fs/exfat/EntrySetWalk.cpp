// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/exfat/EntrySetWalk.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "fs/ClusterChain.hpp"
#include "fs/DirectoryTreeWalk.hpp"
#include "fs/exfat/PendingSet.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/exfat/DirectoryEntry.hpp"

namespace revenant::fs::exfat {

namespace {

// Where the walk is.
struct Cursor {
	std::string path;
	std::uint32_t cluster;
	unsigned depth;
};

// The slots up to the first one never used. exFAT's rule is FAT's: a zero type
// byte ends the directory.
[[nodiscard]] std::span<const std::byte> upToEnd(std::span<const std::byte> bytes) {
	for (std::size_t at = 0; at + kDirectoryEntryBytes <= bytes.size();
		 at += kDirectoryEntryBytes) {
		const auto header = classifyExfatEntry(bytes.subspan(at, kDirectoryEntryBytes));
		if (header.hasValue() && header.value().kind == ExfatEntryKind::kEndOfDirectory) {
			return bytes.first(at);
		}
	}
	return bytes;
}

// The tree, walked from an explicit worklist rather than by recursion: every
// cluster number it follows came off the disk.
class Walk {
public:
	Walk(const ClusterChain& chain, EntryVisitor& visitor) noexcept
		: chain_(&chain), visitor_(&visitor) {}

	[[nodiscard]] Result<EnumerationStats> run(std::uint32_t rootCluster) {
		start(rootCluster);
		return driveWorklist(pending_, [this](const Cursor& cursor) { return walkOne(cursor); })
			.map([this](std::uint64_t reported) {
				return EnumerationStats{
					.recordsScanned = scanned_,
					.entriesReported = reported,
					.nonConformingVolume = false};
			});
	}

private:
	void start(std::uint32_t rootCluster) {
		visited_.push_back(rootCluster);
		pending_.push_back(Cursor{.path = {}, .cluster = rootCluster, .depth = 0});
	}

	[[nodiscard]] Result<std::vector<std::byte>> readDirectory(std::uint32_t cluster) {
		return chain_->chainFrom(cluster).andThen(
			[this](const std::vector<std::uint32_t>& clusters) {
				return readDirectoryBytes(*chain_, clusters, kMaxDirectoryBytes);
			});
	}

	[[nodiscard]] Result<std::uint64_t> walkOne(const Cursor& cursor) {
		return walkOneDirectory(
			[&] { return readDirectory(cursor.cluster); },
			[&](std::span<const std::byte> bytes) { return walkSlots(cursor, bytes); });
	}

	[[nodiscard]] std::uint64_t walkSlots(const Cursor& cursor, std::span<const std::byte> bytes) {
		PendingSet set;
		const auto reported =
			foldSlots(upToEnd(bytes), kDirectoryEntryBytes, [&](std::span<const std::byte> slot) {
				return visitSlot(cursor, set, slot);
			});
		return reported + finish(cursor, set);
	}

	[[nodiscard]] std::uint64_t
	visitSlot(const Cursor& cursor, PendingSet& set, std::span<const std::byte> slot) {
		++scanned_;
		const auto header = classifyExfatEntry(slot);
		if (!header.hasValue()) {
			return 0;
		}
		if (header.value().kind == ExfatEntryKind::kFile) {
			return startSet(cursor, set, header.value().inUse, slot);
		}
		set.absorb(header.value().kind, slot);
		return 0;
	}

	// A set ends where the next one begins.
	[[nodiscard]] std::uint64_t
	startSet(const Cursor& cursor, PendingSet& set, bool inUse, std::span<const std::byte> slot) {
		const auto reported = finish(cursor, set);
		set.begin(inUse, slot);
		return reported;
	}

	// A set is complete when the next one begins or the directory ends. exFAT
	// states how many entries follow, but a damaged set may not deliver them —
	// so what is there is used, and the count is not trusted to arrive.
	[[nodiscard]] std::uint64_t finish(const Cursor& cursor, PendingSet& set) {
		const auto assembled = set.take();
		if (!assembled.has_value()) {
			return 0;
		}
		if (assembled->isDirectory) {
			enqueue(cursor, *assembled);
			return 0;
		}
		visitor_->onEntry(entryOf(cursor.path, *assembled, *chain_));
		return 1;
	}

	void enqueue(const Cursor& cursor, const AssembledSet& set) {
		const bool walkable = cursor.depth < kMaxDirectoryDepth &&
							  chain_->isDataCluster(set.firstCluster) &&
							  std::ranges::find(visited_, set.firstCluster) == visited_.end();
		if (!walkable) {
			return;
		}
		visited_.push_back(set.firstCluster);
		pending_.push_back(
			Cursor{
				.path = joinedPath(cursor.path, set.name.utf8),
				.cluster = set.firstCluster,
				.depth = cursor.depth + 1});
	}

	const ClusterChain* chain_; // non-owning, never null
	EntryVisitor* visitor_;     // non-owning, never null
	std::vector<Cursor> pending_;
	std::vector<std::uint32_t> visited_;
	std::uint64_t scanned_ = 0;
};

} // namespace

Result<EnumerationStats>
walkVolume(const ClusterChain& chain, std::uint32_t rootCluster, EntryVisitor& visitor) {
	Walk walk{chain, visitor};
	return walk.run(rootCluster);
}

} // namespace revenant::fs::exfat

// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ext4/DirectoryWalk.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fs/DirectoryTreeWalk.hpp"
#include "fs/ext4/BlockReader.hpp"
#include "fs/ext4/DirectoryBytes.hpp"
#include "fs/ext4/DirectoryHole.hpp"
#include "fs/ext4/EntryFromInode.hpp"
#include "fs/ext4/InodeTable.hpp"
#include "fs/ext4/OrphanPass.hpp"
#include "fs/ext4/WalkCursor.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ext4/DirectoryEntry.hpp"
#include "revenant/fs/ext4/Inode.hpp"
#include "revenant/fs/ext4/Superblock.hpp"

namespace revenant::fs::ext4 {

namespace {

// The tree, walked from an explicit worklist rather than by recursion: every
// inode number it follows came off the disk.
class Walk {
public:
	Walk(const EntrySource& source, EntryVisitor& visitor) noexcept
		: source_(source), visitor_(&visitor) {}

	[[nodiscard]] Result<EnumerationStats> run() {
		start();
		return driveWorklist(pending_, [this](const Cursor& cursor) { return walkOne(cursor); })
			.map([this](std::uint64_t reported) { return statsOf(reported + takeOrphans()); });
	}

private:
	void start() {
		visited_.push_back(kRootInode);
		pending_.push_back(Cursor{.path = {}, .inode = kRootInode, .depth = 0});
	}

	[[nodiscard]] EnumerationStats statsOf(std::uint64_t reported) const {
		return EnumerationStats{
			.recordsScanned = scanned_,
			.entriesReported = reported,
			.nonConformingVolume = false};
	}

	[[nodiscard]] Result<std::uint64_t> walkOne(const Cursor& cursor) {
		return walkOneDirectory(
			[&] { return readDirectoryBytes(source_, cursor.inode); },
			[&](std::span<const std::byte> bytes) { return walkBlocks(cursor, bytes); });
	}

	// A directory record never crosses a block boundary, so every block is walked
	// from its own first record rather than the content being read as one run.
	[[nodiscard]] std::uint64_t walkBlocks(const Cursor& cursor, std::span<const std::byte> bytes) {
		return foldSlots(
			bytes,
			source_.blocks->geometry().blockSizeBytes,
			[&](std::span<const std::byte> block) { return walkBlock(cursor, block); });
	}

	[[nodiscard]] std::uint64_t walkBlock(const Cursor& cursor, std::span<const std::byte> block) {
		BlockCursor walking{.at = 0, .reported = 0};
		while (walking.at + kDirEntryHeaderBytes <= block.size() &&
			   step(cursor, block.subspan(walking.at), walking)) {
			// The step itself moved the cursor; the loop only asks whether it can
			// move again.
		}
		return walking.reported;
	}

	// One record, folded into `walking`. False when the block is over: a record
	// that will not parse has no length, and the walk has nowhere to go but a
	// guess.
	[[nodiscard]] bool
	step(const Cursor& cursor, std::span<const std::byte> at, BlockCursor& walking) {
		const RecordStep taken = visitRecord(cursor, at);
		walking.at += taken.bytes;
		walking.reported += taken.reported;
		return taken.bytes != 0;
	}

	// A record that will not parse ends this block: its length is what says where
	// the next one is, and without it the walk has nowhere to go but a guess.
	[[nodiscard]] RecordStep visitRecord(const Cursor& cursor, std::span<const std::byte> at) {
		const auto parsed = parseExt4DirEntry(at);
		if (!parsed.hasValue()) {
			return RecordStep{.bytes = 0, .reported = 0};
		}
		++scanned_;
		const Ext4DirEntry& entry = parsed.value();
		const auto record = at.first(entry.recordBytes);
		return RecordStep{
			.bytes = entry.recordBytes,
			.reported = visitLive(cursor, entry) + visitHole(cursor, record, entry)};
	}

	[[nodiscard]] std::uint64_t visitLive(const Cursor& cursor, const Ext4DirEntry& entry) {
		if (entry.inode == 0 || isSelfOrParent(entry.nameBytes)) {
			return 0;
		}
		const auto inode = source_.inodes->read(entry.inode);
		if (!inode.hasValue() || inode.value().isUnused) {
			return 0;
		}
		return take(
			cursor,
			foundName(cursor, entry.nameBytes, entry.inode, EntryState::kLive),
			inode.value());
	}

	// The deleted entries the live one's record swallowed. An entry whose own
	// inode number was cleared — the first in a block, which has no neighbour to
	// swallow it — leaves its name behind but nothing to read it with, so the
	// whole record is searched and the nameless remains find nothing.
	[[nodiscard]] std::uint64_t
	visitHole(const Cursor& cursor, std::span<const std::byte> record, const Ext4DirEntry& entry) {
		const auto liveBytes =
			entry.inode == 0 ? record.size() : liveEntryBytes(entry.nameBytes.size());
		std::uint64_t reported = 0;
		const HoleBounds bounds{.liveBytes = liveBytes, .inodeCount = inodeCount()};
		for (const HoleEntry& hole : deletedEntriesIn(record, bounds)) {
			reported += visitDeleted(cursor, hole);
		}
		return reported;
	}

	[[nodiscard]] std::uint64_t visitDeleted(const Cursor& cursor, const HoleEntry& hole) {
		const auto inode = source_.inodes->read(hole.inode);
		if (!inode.hasValue() || inode.value().isUnused || inode.value().isDirectory) {
			return 0;
		}
		report(foundName(cursor, hole.nameBytes, hole.inode, EntryState::kDeleted), inode.value());
		return 1;
	}

	// Reports a file, or enqueues a directory. A directory is a place, not an
	// entry, and is never reported as one.
	[[nodiscard]] std::uint64_t
	take(const Cursor& cursor, const FoundName& found, const Ext4Inode& inode) {
		if (inode.isDirectory) {
			enqueue(cursor, found, inode);
			return 0;
		}
		report(found, inode);
		return 1;
	}

	// Only a *deleted* inode can also be on the orphan list, so only those are
	// remembered for the pass that follows. A live file's number would never be
	// looked for, and keeping every one of them would make the orphan pass
	// quadratic in the size of the volume.
	void report(const FoundName& found, const Ext4Inode& inode) {
		if (inode.isDeleted) {
			reported_.push_back(found.inodeNumber);
		}
		visitor_->onEntry(entryOf(source_, found, inode));
	}

	void enqueue(const Cursor& cursor, const FoundName& found, const Ext4Inode& inode) {
		const bool walkable = cursor.depth < kMaxDirectoryDepth && !inode.isDeleted &&
							  std::ranges::find(visited_, found.inodeNumber) == visited_.end();
		if (!walkable) {
			return;
		}
		visited_.push_back(found.inodeNumber);
		pending_.push_back(
			Cursor{.path = found.path, .inode = found.inodeNumber, .depth = cursor.depth + 1});
	}

	// The inodes the tree cannot reach, once it has been walked. The deleted
	// names the walk already found are what tell an orphan from a file it has
	// reported under a name.
	[[nodiscard]] std::uint64_t takeOrphans() {
		const auto pass = reportOrphans(source_, *visitor_, reported_);
		scanned_ += pass.scanned;
		return pass.reported;
	}

	[[nodiscard]] std::uint32_t inodeCount() const {
		return source_.blocks->geometry().totalInodes;
	}

	EntrySource source_;
	EntryVisitor* visitor_; // non-owning, never null
	std::vector<Cursor> pending_;
	std::vector<std::uint32_t> visited_;
	std::vector<std::uint32_t> reported_;
	std::uint64_t scanned_ = 0;
};

} // namespace

Result<EnumerationStats> walkVolume(const EntrySource& source, EntryVisitor& visitor) {
	Walk walk{source, visitor};
	return walk.run();
}

} // namespace revenant::fs::ext4

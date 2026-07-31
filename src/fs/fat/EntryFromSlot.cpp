// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/fat/EntryFromSlot.hpp"

#include <utility>
#include <vector>

#include "fs/ClusterChain.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/fat/DirectoryEntry.hpp"

namespace revenant::fs::fat {

namespace {

[[nodiscard]] std::vector<Extent> locate(const ClusterChain& table, const ShortEntry& entry) {
	if (entry.sizeInBytes == 0 || !table.isDataCluster(entry.firstCluster)) {
		return {};
	}
	// A live file still has its chain. What is left of a deleted one is its
	// first cluster and the assumption that the rest followed it — wrong for a
	// fragmented file, which is why nothing read that way is graded better than
	// uncertain.
	return entry.deleted ? extentsAssumingContiguous(table, entry.firstCluster, entry.sizeInBytes)
						 : extentsFollowingChain(table, entry.firstCluster, entry.sizeInBytes);
}

[[nodiscard]] EntryState stateOf(const ShortEntry& entry, const EntryPlace& place) {
	if (place.underDeleted) {
		return EntryState::kOrphaned;
	}
	return entry.deleted ? EntryState::kDeleted : EntryState::kLive;
}

// Grading is on metadata integrity alone. A guessed name, guessed extents, or a
// guessed place in the tree each make the record something less than trusted,
// and anything short of trusted is territory the carve pass covers as well.
[[nodiscard]] Confidence
gradeOf(const ShortEntry& entry, const EntryPlace& place, const std::vector<Extent>& extents) {
	const bool guessed = entry.deleted || place.underDeleted || !place.name.lossless;
	const bool located = !extents.empty() || entry.sizeInBytes == 0;
	return !guessed && located ? Confidence::kValid : Confidence::kUncertain;
}

} // namespace

RecoveredEntry
entryFromSlot(const ClusterChain& table, const ShortEntry& entry, const EntryPlace& place) {
	auto extents = locate(table, entry);
	const auto grade = gradeOf(entry, place, extents);
	return RecoveredEntry{
		.path = place.path,
		.sizeInBytes = entry.sizeInBytes,
		.extents = std::move(extents),
		.residentContent = {},
		.timestamps = entry.timestamps,
		.state = stateOf(entry, place),
		.recoverability = grade};
}

} // namespace revenant::fs::fat

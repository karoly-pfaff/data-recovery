// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/fat/EntryFromSlot.hpp"

#include <utility>
#include <vector>

#include "fs/fat/ChainExtents.hpp"
#include "fs/fat/FatTable.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/fat/DirectoryEntry.hpp"

namespace revenant::fs::fat {

namespace {

// The chain a live file still has. Anything that will not follow leaves the
// extents empty rather than guessing.
[[nodiscard]] std::vector<Extent> fromChain(const FatTable& table, const ShortEntry& entry) {
	const auto chain = table.chainFrom(entry.firstCluster);
	if (!chain.hasValue()) {
		return {};
	}
	const auto extents = chainExtents(chain.value(), table.geometry(), entry.sizeInBytes);
	return extents.hasValue() ? extents.value() : std::vector<Extent>{};
}

// What is left of a deleted file: its first cluster, and the assumption that
// the rest followed it. Wrong for a fragmented file, which is why nothing read
// this way is graded better than uncertain.
[[nodiscard]] std::vector<Extent> fromContiguity(const FatTable& table, const ShortEntry& entry) {
	const auto extents = contiguousExtents(entry.firstCluster, table.geometry(), entry.sizeInBytes);
	return extents.hasValue() ? extents.value() : std::vector<Extent>{};
}

[[nodiscard]] std::vector<Extent> locate(const FatTable& table, const ShortEntry& entry) {
	if (entry.sizeInBytes == 0 || !table.isDataCluster(entry.firstCluster)) {
		return {};
	}
	return entry.deleted ? fromContiguity(table, entry) : fromChain(table, entry);
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
entryFromSlot(const FatTable& table, const ShortEntry& entry, const EntryPlace& place) {
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

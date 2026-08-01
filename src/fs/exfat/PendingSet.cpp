// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/exfat/PendingSet.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "fs/ClusterChain.hpp"
#include "fs/DirectoryTreeWalk.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Utf16Name.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/exfat/DirectoryEntry.hpp"

namespace revenant::fs::exfat {

namespace {

// The name is as long as the stream extension says, whatever the fragments
// carried after it — the tail of the last fragment is padding, not name.
[[nodiscard]] DecodedName nameOf(const std::vector<std::byte>& raw, std::uint8_t nameLength) {
	const auto wanted = std::min<std::size_t>(raw.size(), std::size_t{nameLength} * 2);
	return decodeUtf16Name(std::span{raw}.first(wanted));
}

// A deleted set whose clusters the volume has since handed out again no longer
// holds what its entry claims.
[[nodiscard]] bool overwritten(const SetSource& source, const AssembledSet& set) {
	return !set.inUse && source.bitmap->known() && source.bitmap->isAllocated(set.firstCluster);
}

[[nodiscard]] std::vector<Extent> locate(const SetSource& source, const AssembledSet& set) {
	const auto& chain = *source.chain;
	if (set.sizeInBytes == 0 || !chain.isDataCluster(set.firstCluster)) {
		return {};
	}
	if (overwritten(source, set)) {
		return {};
	}
	// A set that used the table is followed through it; one that declared itself
	// contiguous is taken at its word — which is what a deleted exFAT file still
	// has and a deleted FAT32 file does not.
	return set.contiguous ? extentsAssumingContiguous(chain, set.firstCluster, set.sizeInBytes)
						  : extentsFollowingChain(chain, set.firstCluster, set.sizeInBytes);
}

// Grading is on metadata integrity alone. A deleted set kept its whole name, so
// what makes it uncertain is that its clusters may since have been handed out —
// which is the allocation bitmap's question, and the carve pass covers it.
[[nodiscard]] Confidence gradeOf(const AssembledSet& set, const std::vector<Extent>& extents) {
	const bool located = !extents.empty() || set.sizeInBytes == 0;
	return set.inUse && set.name.lossless && located ? Confidence::kValid : Confidence::kUncertain;
}

} // namespace

void PendingSet::begin(bool inUse, std::span<const std::byte> slot) {
	const auto entry = parseFileEntry(slot);
	nameBytes_.clear();
	hasStream_ = false;
	active_ = entry.hasValue();
	inUse_ = inUse;
	file_ = entry.hasValue() ? entry.value() : FileEntry{};
}

void PendingSet::takeStream(std::span<const std::byte> slot) {
	const auto stream = parseStreamExtension(slot);
	hasStream_ = stream.hasValue();
	stream_ = stream.hasValue() ? stream.value() : StreamExtension{};
}

void PendingSet::absorb(ExfatEntryKind kind, std::span<const std::byte> slot) {
	if (!active_) {
		return;
	}
	if (kind == ExfatEntryKind::kStreamExtension) {
		takeStream(slot);
	}
	if (kind == ExfatEntryKind::kFileName) {
		appendName(slot);
	}
}

void PendingSet::appendName(std::span<const std::byte> slot) {
	const auto fragment = parseFileName(slot);
	if (fragment.hasValue()) {
		nameBytes_.insert(nameBytes_.end(), fragment.value().begin(), fragment.value().end());
	}
}

std::optional<AssembledSet> PendingSet::take() {
	if (!active_ || !hasStream_) {
		active_ = false;
		return std::nullopt;
	}
	active_ = false;
	return AssembledSet{
		.name = nameOf(nameBytes_, stream_.nameLength),
		.firstCluster = stream_.firstCluster,
		.sizeInBytes = stream_.dataLength,
		.timestamps = file_.timestamps,
		.isDirectory = file_.isDirectory,
		.inUse = inUse_,
		.contiguous = stream_.noFatChain};
}

RecoveredEntry
entryOf(const std::string& parentPath, const AssembledSet& set, const SetSource& source) {
	auto extents = locate(source, set);
	const auto grade = gradeOf(set, extents);
	return RecoveredEntry{
		.path = joinedPath(parentPath, set.name.utf8),
		.sizeInBytes = set.sizeInBytes,
		.extents = std::move(extents),
		.residentContent = {},
		.timestamps = set.timestamps,
		.state = set.inUse ? EntryState::kLive : EntryState::kDeleted,
		.recoverability = grade};
}

} // namespace revenant::fs::exfat

// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ntfs/EntryFromRecord.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "fs/ntfs/EntryPath.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ntfs/BootSector.hpp"
#include "revenant/fs/ntfs/MftRecord.hpp"
#include "revenant/fs/ntfs/Runlist.hpp"

namespace revenant::fs::ntfs {

namespace {

// A `$DATA` attribute resolved to bytes we can actually reach. `located` is
// false when the runs will not map — sparse, or reaching past the volume — in
// which case there is nothing here to hand back, by design.
struct EntryContent {
	std::vector<Extent> extents;
	std::vector<std::byte> resident;
	std::uint64_t sizeBytes;
	bool located;
};

[[nodiscard]] EntryContent noContent() {
	return EntryContent{.extents = {}, .resident = {}, .sizeBytes = 0, .located = false};
}

[[nodiscard]] EntryContent nonResidentContent(const MftData& data, const NtfsGeometry& geometry) {
	const auto extents = decodeRunlist(data.runlistBytes).andThen([&](const Runlist& runlist) {
		return runlistExtents(runlist, geometry, data.realSize);
	});
	if (!extents.hasValue()) {
		return EntryContent{
			.extents = {},
			.resident = {},
			.sizeBytes = data.realSize,
			.located = false};
	}
	return EntryContent{
		.extents = extents.value(),
		.resident = {},
		.sizeBytes = data.realSize,
		.located = true};
}

[[nodiscard]] EntryContent dataContent(const MftData& data, const NtfsGeometry& geometry) {
	if (!data.resident) {
		return nonResidentContent(data, geometry);
	}
	return EntryContent{
		.extents = {},
		.resident = data.residentContent,
		.sizeBytes = data.realSize,
		.located = true};
}

[[nodiscard]] EntryContent contentOf(const MftRecordView& view, const NtfsGeometry& geometry) {
	if (!view.data.has_value()) {
		return noContent();
	}
	return dataContent(view.data.value(), geometry);
}

// A chain that never reached the root is orphaned whether or not the record is
// still in use: without a parent there is no place in the tree to put it back.
[[nodiscard]] EntryState stateOf(const MftRecordView& view, const EntryPath& path) noexcept {
	if (!path.reachedRoot) {
		return EntryState::kOrphaned;
	}
	return view.inUse ? EntryState::kLive : EntryState::kDeleted;
}

// Metadata integrity only. Whether the clusters have since been reallocated
// needs `$Bitmap`, which this milestone does not parse; that question belongs
// to the entry's state, not to how far its metadata can be trusted.
[[nodiscard]] Confidence
gradeOf(const MftRecordView& view, const EntryPath& path, bool located) noexcept {
	const bool intact = view.grade == Confidence::kValid && path.reachedRoot && located;
	return intact ? Confidence::kValid : Confidence::kUncertain;
}

} // namespace

RecoveredEntry
entryFromRecord(const MftRecordView& view, const EntryPath& path, const NtfsGeometry& geometry) {
	EntryContent content = contentOf(view, geometry);
	return RecoveredEntry{
		.path = path.path,
		.sizeInBytes = content.sizeBytes,
		.extents = std::move(content.extents),
		.residentContent = std::move(content.resident),
		.timestamps = view.standardInfo.value_or(Timestamps{}),
		.state = stateOf(view, path),
		.recoverability = gradeOf(view, path, content.located)};
}

} // namespace revenant::fs::ntfs

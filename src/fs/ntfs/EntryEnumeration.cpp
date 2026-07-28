// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/ntfs/EntryEnumeration.hpp"

#include <cstdint>

#include "fs/ntfs/EntryFromRecord.hpp"
#include "fs/ntfs/EntryPath.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/ntfs/MftRecord.hpp"
#include "revenant/fs/ntfs/MftTable.hpp"

namespace revenant::fs::ntfs {

namespace {

// A directory has no content to give back, a record with no `$DATA` has no
// bytes to point at, and a record with no name has no path — none of the three
// is a file a user can be handed.
[[nodiscard]] bool isRecoverableFile(const MftRecordView& view) {
	return !view.isDirectory && view.data.has_value() && !view.names.empty();
}

[[nodiscard]] bool
reportRecord(const MftTable& table, const MftRecordView& view, EntryVisitor& visitor) {
	if (!isRecoverableFile(view)) {
		return false;
	}
	visitor.onEntry(entryFromRecord(view, resolveEntryPath(table, view), table.geometry()));
	return true;
}

// Whether one slot produced an entry — and the one failure that is not the
// slot's own business: a device that will not read.
[[nodiscard]] Result<bool>
stepRecord(const MftTable& table, std::uint64_t number, EntryVisitor& visitor) {
	const auto parsed = table.readRecord(number);
	if (parsed.hasValue()) {
		return reportRecord(table, parsed.value(), visitor);
	}
	if (parsed.error().code == ErrorCode::kIoFailure) {
		return parsed.error();
	}
	return false;
}

// One slot walked, folded into the running totals.
[[nodiscard]] Result<EnumerationStats> countRecord(
	const MftTable& table,
	std::uint64_t number,
	EntryVisitor& visitor,
	EnumerationStats stats) {
	const auto reported = stepRecord(table, number, visitor);
	if (!reported.hasValue()) {
		return reported.error();
	}
	return EnumerationStats{
		.recordsScanned = stats.recordsScanned + 1,
		.entriesReported = stats.entriesReported + (reported.value() ? 1U : 0U)};
}

} // namespace

Result<EnumerationStats> enumerateEntries(const MftTable& table, EntryVisitor& visitor) {
	Result<EnumerationStats> stats = EnumerationStats{.recordsScanned = 0, .entriesReported = 0};
	for (std::uint64_t number = kFirstUserRecord; stats.hasValue() && number < table.recordCount();
		 ++number) {
		stats = countRecord(table, number, visitor, stats.value());
	}
	return stats;
}

} // namespace revenant::fs::ntfs

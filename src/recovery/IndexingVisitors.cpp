// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/IndexingVisitors.hpp"

#include <cstdint>

#include "revenant/carve/ScanCandidate.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/recovery/Candidate.hpp"
#include "revenant/recovery/CandidateIndex.hpp"

namespace revenant::recovery {

namespace {

[[nodiscard]] Candidate candidateOf(const fs::RecoveredEntry& entry) {
	return Candidate{
		.name = entry.path,
		.extents = entry.extents,
		.residentContent = entry.residentContent,
		.timestamps = entry.timestamps,
		.confidence = entry.recoverability,
		.source = CandidateSource::kFilesystem};
}

// A carved file has no name of its own and no timestamps to preserve — its
// extension is all the naming information there is, which is exactly the
// difference a named recovery is worth.
[[nodiscard]] Candidate candidateOf(const carve::ScanCandidate& found) {
	return Candidate{
		.name = found.result.extension,
		.extents = {fs::Extent{.deviceOffset = found.offset, .lengthBytes = found.result.length}},
		.residentContent = {},
		.timestamps = fs::Timestamps{.created = 0, .modified = 0, .accessed = 0},
		.confidence = found.result.confidence,
		.source = CandidateSource::kCarve};
}

} // namespace

IndexingEntryVisitor::IndexingEntryVisitor(CandidateIndex& index) noexcept : index_(&index) {}

void IndexingEntryVisitor::onEntry(const fs::RecoveredEntry& entry) {
	failed_ += index_->append(candidateOf(entry)).hasValue() ? 0U : 1U;
}

std::uint64_t IndexingEntryVisitor::failedAppends() const noexcept {
	return failed_;
}

IndexingCandidateVisitor::IndexingCandidateVisitor(CandidateIndex& index) noexcept
	: index_(&index) {}

void IndexingCandidateVisitor::onCandidate(const carve::ScanCandidate& candidate) {
	if (candidate.result.confidence == Confidence::kRejected) {
		return;
	}
	failed_ += index_->append(candidateOf(candidate)).hasValue() ? 0U : 1U;
}

std::uint64_t IndexingCandidateVisitor::failedAppends() const noexcept {
	return failed_;
}

} // namespace revenant::recovery

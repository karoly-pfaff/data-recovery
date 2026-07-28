// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "revenant/carve/CandidateVisitor.hpp"
#include "revenant/carve/ScanCandidate.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/recovery/CandidateIndex.hpp"

namespace revenant::recovery {

// The two adapters that put a hybrid run's findings into one index, so the
// orchestrator never has to learn what an index is.
//
// A visitor cannot report a failure back to the pass that called it, so a
// failed append is counted and offered here instead of being swallowed: an
// index that quietly lost records would make every later answer wrong.

class IndexingEntryVisitor final : public fs::EntryVisitor {
public:
	explicit IndexingEntryVisitor(CandidateIndex& index) noexcept;

	void onEntry(const fs::RecoveredEntry& entry) override;

	[[nodiscard]] std::uint64_t failedAppends() const noexcept;

private:
	CandidateIndex* index_; // non-owning, never null
	std::uint64_t failed_ = 0;
};

// Rejected carve results are not indexed: the carver already refused them, and
// keeping them would only pad the index and the manifest with noise.
class IndexingCandidateVisitor final : public carve::CandidateVisitor {
public:
	explicit IndexingCandidateVisitor(CandidateIndex& index) noexcept;

	void onCandidate(const carve::ScanCandidate& candidate) override;

	[[nodiscard]] std::uint64_t failedAppends() const noexcept;

private:
	CandidateIndex* index_; // non-owning, never null
	std::uint64_t failed_ = 0;
};

} // namespace revenant::recovery

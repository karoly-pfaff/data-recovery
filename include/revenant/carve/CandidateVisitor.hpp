// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "revenant/carve/ScanCandidate.hpp"

namespace revenant::carve {

// Receives every candidate the scan discovers. Implementations decide what
// discovery means (collect, index, count) — the scanner never extracts.
class CandidateVisitor {
public:
	virtual ~CandidateVisitor() = default;
	CandidateVisitor() = default;
	CandidateVisitor(const CandidateVisitor&) = delete;
	CandidateVisitor& operator=(const CandidateVisitor&) = delete;
	CandidateVisitor(CandidateVisitor&&) = delete;
	CandidateVisitor& operator=(CandidateVisitor&&) = delete;

	virtual void onCandidate(const ScanCandidate& candidate) = 0;
};

} // namespace revenant::carve

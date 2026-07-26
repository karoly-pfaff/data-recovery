// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include "revenant/carve/CandidateVisitor.hpp"
#include "revenant/carve/ScanCandidate.hpp"

namespace revenant::testing {

// Test double: records every reported candidate for assertions.
class CollectingVisitor final : public carve::CandidateVisitor {
public:
	void onCandidate(const carve::ScanCandidate& candidate) override {
		candidates_.push_back(candidate);
	}

	[[nodiscard]] const std::vector<carve::ScanCandidate>& candidates() const noexcept {
		return candidates_;
	}

private:
	std::vector<carve::ScanCandidate> candidates_;
};

} // namespace revenant::testing

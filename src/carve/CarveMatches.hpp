// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to SignatureScanner's scan — running the owning carver at each
// surviving signature match in one window, and reporting what it concluded.
// NOT a public interface; consumed only from src/carve/.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "WindowMatch.hpp"
#include "revenant/carve/CandidateVisitor.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::carve {

// What running every surviving match in one window produced.
struct MatchOutcome {
	std::uint64_t resumeCursor;
	std::size_t candidatesReported;
};

// Runs each match in order, threading the resume cursor forward so a match
// already covered by an earlier candidate's extent is skipped without a carve
// attempt. Reports every surviving candidate to `visitor` — discovery only.
Result<MatchOutcome> processMatches(
	BlockDevice& device,
	CandidateVisitor& visitor,
	std::span<const Match> matches,
	std::vector<std::byte>& carveBuffer);

} // namespace revenant::carve

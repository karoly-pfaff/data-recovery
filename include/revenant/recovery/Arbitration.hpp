// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/recovery/Candidate.hpp"

namespace revenant::recovery {

// What arbitration decided.
struct Arbitration {
	// The winners, in device order — the only candidates that will ever be
	// materialized (ADR-0006).
	std::vector<Candidate> winners;

	// How many candidates a better explanation of the same bytes displaced.
	// Counted rather than dropped quietly: "why is this file not in the
	// output" is a question a recovery tool has to be able to answer.
	std::uint64_t suppressed;
};

// Resolves competing explanations of the same bytes.
//
// Candidates are considered most-trusted first; one whose extents are all
// still free wins and claims them, and one overlapping an already-claimed
// region loses. It loses *whole*: a partial overlap makes a candidate a worse
// explanation of the same region, not a good explanation of the rest, and
// accepting it piecewise would emit exactly the fragments arbitration exists
// to remove.
//
// A filesystem entry beats a carve of the same bytes outright, not on a tie.
// The two confidence scales do not mean the same thing: a carver grades the
// structure of bytes it can see, while a filesystem entry knows the name, the
// timestamps and — decisively — which runs the content is spread across, so a
// carve starting at a fragmented file's first run would hand back garbage
// however perfect it looked. After source comes confidence, then the lower
// offset, then the larger candidate; the last two are arbitrary, and stating
// them is what makes a run reproducible. A `kRejected` candidate never wins,
// and one with no extents always does: resident content occupies no device
// region it could lose.
[[nodiscard]] Arbitration arbitrate(std::vector<Candidate> candidates);

// The same over a session directory's index, read back first.
[[nodiscard]] Result<Arbitration> arbitrateIndex(const std::filesystem::path& directory);

} // namespace revenant::recovery

// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery/WriteOrder.hpp"

#include <cstdint>
#include <span>
#include <vector>

#include "revenant/recovery/Candidate.hpp"

namespace revenant::recovery {

namespace {

// The winners of one source, each carrying the ordinal it has in device order.
[[nodiscard]] std::vector<Ordered>
ofSource(std::span<const Candidate> winners, CandidateSource source) {
	std::vector<Ordered> ordered;
	std::uint64_t ordinal = 0;
	for (const Candidate& winner : winners) {
		if (winner.source == source) {
			ordered.push_back(Ordered{.winner = &winner, .ordinal = ordinal});
		}
		++ordinal;
	}
	return ordered;
}

} // namespace

std::vector<Ordered> orderedForWriting(std::span<const Candidate> winners) {
	std::vector<Ordered> ordered = ofSource(winners, CandidateSource::kFilesystem);
	const auto carved = ofSource(winners, CandidateSource::kCarve);
	ordered.insert(ordered.end(), carved.begin(), carved.end());
	return ordered;
}

} // namespace revenant::recovery

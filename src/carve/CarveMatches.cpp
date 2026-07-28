// SPDX-License-Identifier: GPL-3.0-or-later
#include "CarveMatches.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "WindowMatch.hpp"
#include "revenant/carve/CandidateVisitor.hpp"
#include "revenant/carve/Plausibility.hpp"
#include "revenant/carve/ScanCandidate.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::carve {

namespace {

// Runs the carver over its own bounded window read from the device; the
// buffer is already sized to the configured carve bound by the caller.
// The plausibility floor is applied here, at the one place a carve result
// enters the scan: a structurally perfect match that is far too small to be a
// real file of its kind is reported as Rejected rather than as a recovery.
Result<ScanCandidate> buildCandidate(const Match& match, ByteReader& reader) {
	const auto result = match.carver->carve(reader);
	if (!result.hasValue()) {
		return result.error();
	}
	return ScanCandidate{.offset = match.offset, .result = applyPlausibility(result.value())};
}

Result<ScanCandidate>
attemptCarve(BlockDevice& device, const Match& match, std::vector<std::byte>& carveBuffer) {
	const auto window = readWindow(device, match.offset, carveBuffer);
	if (!window.hasValue()) {
		return window.error();
	}
	ByteReader reader{window.value()};
	return buildCandidate(match, reader);
}

// The offset scanning resumes from after a candidate: past the extent for
// trusted verdicts, one byte forward for rejections.
std::uint64_t resumeOffset(const ScanCandidate& candidate) {
	if (candidate.result.confidence == Confidence::kRejected) {
		return candidate.offset + 1;
	}
	return candidate.offset + std::max<std::uint64_t>(candidate.result.length, 1);
}

// Carves `match` unless it falls inside the extent a previous candidate in
// this window already resumed past (the straddle/resume dedupe).
Result<MatchOutcome> applyMatch(
	BlockDevice& device,
	CandidateVisitor& visitor,
	const Match& match,
	std::vector<std::byte>& carveBuffer,
	MatchOutcome outcome) {
	if (match.offset < outcome.resumeCursor) {
		return outcome;
	}
	const auto candidate = attemptCarve(device, match, carveBuffer);
	if (!candidate.hasValue()) {
		return candidate.error();
	}
	visitor.onCandidate(candidate.value());
	return MatchOutcome{
		.resumeCursor = resumeOffset(candidate.value()),
		.candidatesReported = outcome.candidatesReported + 1};
}

} // namespace

Result<MatchOutcome> processMatches(
	BlockDevice& device,
	CandidateVisitor& visitor,
	std::span<const Match> matches,
	std::vector<std::byte>& carveBuffer) {
	MatchOutcome outcome{.resumeCursor = 0, .candidatesReported = 0};
	for (const Match& match : matches) {
		const auto next = applyMatch(device, visitor, match, carveBuffer, outcome);
		if (!next.hasValue()) {
			return next.error();
		}
		outcome = next.value();
	}
	return outcome;
}

} // namespace revenant::carve

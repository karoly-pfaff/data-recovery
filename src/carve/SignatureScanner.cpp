// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/carve/SignatureScanner.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "WindowMatch.hpp"
#include "revenant/carve/CandidateVisitor.hpp"
#include "revenant/carve/CarveResult.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/Plausibility.hpp"
#include "revenant/carve/ScanCandidate.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::carve {

namespace {

// The result of running every surviving match in a window through its carver.
struct MatchOutcome {
	std::uint64_t resumeCursor;
	std::size_t candidatesReported;
};

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

// Runs applyMatch over every match in order, threading the resume cursor
// forward so a covered match is skipped without a carve attempt.
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

// Next window start: slide back by the overlap, but never behind a resume
// point a surviving candidate already reported past. Always strictly
// advances past `cursor`, even on a short tail window (no infinite loop).
std::uint64_t nextCursor(
	std::uint64_t cursor,
	std::uint64_t windowEnd,
	std::size_t overlap,
	std::uint64_t resumeCursor) {
	const auto slidBack = std::max<std::uint64_t>(windowEnd - overlap, resumeCursor);
	return slidBack > cursor ? slidBack : windowEnd;
}

} // namespace

SignatureScanner::SignatureScanner(const CarverRegistry& registry, ScanConfig config) noexcept
	: registry_(&registry), config_(config) {}

Result<SignatureScanner::WindowStep> SignatureScanner::stepWindow(
	BlockDevice& device,
	CandidateVisitor& visitor,
	std::uint64_t cursor,
	const WindowMatches& read,
	ScanBuffers& buffers) {
	if (read.bytesRead == 0) {
		return WindowStep{.nextCursor = device.sizeInBytes(), .candidatesReported = 0};
	}
	const auto outcome = processMatches(device, visitor, read.matches, buffers.carve);
	if (!outcome.hasValue()) {
		return outcome.error();
	}
	return WindowStep{
		.nextCursor =
			nextCursor(cursor, cursor + read.bytesRead, read.overlap, outcome.value().resumeCursor),
		.candidatesReported = outcome.value().candidatesReported};
}

Result<SignatureScanner::WindowStep> SignatureScanner::scanOneWindow(
	BlockDevice& device,
	CandidateVisitor& visitor,
	std::uint64_t cursor,
	ScanBuffers& buffers) const {
	const auto read = readAndMatch(device, cursor, buffers.window, *registry_);
	if (!read.hasValue()) {
		return read.error();
	}
	return stepWindow(device, visitor, cursor, read.value(), buffers);
}

Result<std::uint64_t> SignatureScanner::advanceWindow(
	BlockDevice& device,
	CandidateVisitor& visitor,
	std::uint64_t cursor,
	ScanBuffers& buffers,
	ScanStats& stats) const {
	const auto step = scanOneWindow(device, visitor, cursor, buffers);
	if (!step.hasValue()) {
		return step.error();
	}
	stats.candidateCount += step.value().candidatesReported;
	return step.value().nextCursor;
}

Result<ScanStats> SignatureScanner::runScanLoop(
	BlockDevice& device,
	CandidateVisitor& visitor,
	ScanBuffers& buffers) const {
	ScanStats stats{.bytesScanned = device.sizeInBytes(), .candidateCount = 0};
	std::uint64_t cursor = 0;
	while (cursor < device.sizeInBytes()) {
		const auto next = advanceWindow(device, visitor, cursor, buffers, stats);
		if (!next.hasValue()) {
			return next.error();
		}
		cursor = next.value();
	}
	return stats;
}

Result<ScanStats> SignatureScanner::scan(BlockDevice& device, CandidateVisitor& visitor) const {
	ScanBuffers buffers{
		.window = std::vector<std::byte>(config_.windowBytes),
		.carve = std::vector<std::byte>(config_.maxCarveBytes)};
	return runScanLoop(device, visitor, buffers);
}

} // namespace revenant::carve

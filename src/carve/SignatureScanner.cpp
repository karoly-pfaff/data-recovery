// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/carve/SignatureScanner.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "CarveMatches.hpp"
#include "WindowMatch.hpp"
#include "revenant/carve/CandidateVisitor.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::carve {

namespace {

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

// One past the region's last byte, saturating: a length read off a disk may
// not wrap the bound it is supposed to impose.
std::uint64_t endOf(ScanRegion region) noexcept {
	constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
	return region.lengthBytes > kMax - region.offset ? kMax : region.offset + region.lengthBytes;
}

} // namespace

SignatureScanner::SignatureScanner(const CarverRegistry& registry, ScanConfig config) noexcept
	: registry_(&registry), config_(config) {}

Result<SignatureScanner::WindowStep> SignatureScanner::stepWindow(
	const ScanContext& context,
	std::uint64_t cursor,
	const WindowMatches& read,
	ScanBuffers& buffers) {
	if (read.bytesRead == 0) {
		return WindowStep{.nextCursor = endOf(context.region), .candidatesReported = 0};
	}
	const auto outcome =
		processMatches(*context.device, *context.visitor, read.matches, buffers.carve);
	if (!outcome.hasValue()) {
		return outcome.error();
	}
	return WindowStep{
		.nextCursor =
			nextCursor(cursor, cursor + read.bytesRead, read.overlap, outcome.value().resumeCursor),
		.candidatesReported = outcome.value().candidatesReported};
}

std::span<std::byte> SignatureScanner::windowSlice(
	const ScanContext& context,
	std::uint64_t cursor,
	ScanBuffers& buffers) const {
	const std::span<std::byte> window{buffers.window};
	const auto remaining = endOf(context.region) - cursor;
	if (remaining >= window.size()) {
		return window;
	}
	// One signature's reach past the region: a magic whose *candidate* starts
	// on the region's last byte still has to be read whole to be found.
	const auto reach = static_cast<std::size_t>(remaining) + registry_->maxSignatureBytes();
	return window.first(std::min(window.size(), reach));
}

Result<SignatureScanner::WindowStep> SignatureScanner::scanOneWindow(
	const ScanContext& context,
	std::uint64_t cursor,
	ScanBuffers& buffers) const {
	auto read =
		readAndMatch(*context.device, cursor, windowSlice(context, cursor, buffers), *registry_);
	if (!read.hasValue()) {
		return read.error();
	}
	const auto end = endOf(context.region);
	std::erase_if(read.value().matches, [end](const Match& match) { return match.offset >= end; });
	return stepWindow(context, cursor, read.value(), buffers);
}

Result<std::uint64_t> SignatureScanner::advanceWindow(
	const ScanContext& context,
	std::uint64_t cursor,
	ScanBuffers& buffers,
	ScanStats& stats) const {
	const auto step = scanOneWindow(context, cursor, buffers);
	if (!step.hasValue()) {
		return step.error();
	}
	stats.candidateCount += step.value().candidatesReported;
	return step.value().nextCursor;
}

Result<ScanStats>
SignatureScanner::runScanLoop(const ScanContext& context, ScanBuffers& buffers) const {
	ScanStats stats{.bytesScanned = context.region.lengthBytes, .candidateCount = 0};
	Result<std::uint64_t> cursor = context.region.offset;
	while (cursor.hasValue() && cursor.value() < endOf(context.region)) {
		cursor = advanceWindow(context, cursor.value(), buffers, stats);
	}
	if (!cursor.hasValue()) {
		return cursor.error();
	}
	return stats;
}

Result<ScanStats> SignatureScanner::scanRegion(
	BlockDevice& device,
	ScanRegion region,
	CandidateVisitor& visitor) const {
	ScanBuffers buffers{
		.window = std::vector<std::byte>(config_.windowBytes),
		.carve = std::vector<std::byte>(config_.maxCarveBytes)};
	const ScanContext context{.device = &device, .visitor = &visitor, .region = region};
	return runScanLoop(context, buffers);
}

Result<ScanStats> SignatureScanner::scan(BlockDevice& device, CandidateVisitor& visitor) const {
	return scanRegion(
		device,
		ScanRegion{.offset = 0, .lengthBytes = device.sizeInBytes()},
		visitor);
}

} // namespace revenant::carve

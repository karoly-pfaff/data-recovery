// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/carve/CandidateVisitor.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::carve {

// Defined in src/carve/WindowMatch.hpp — internal scan-loop plumbing, not a
// public interface. Forward-declared here only so the private per-window
// helpers below can take them by reference, and so the reusable match buffer
// can be one: `std::vector` accepts an incomplete element type, and every
// place a ScanBuffers is built or destroyed has the definition.
struct WindowMatches;
struct Match;

inline constexpr std::size_t kDefaultScanWindowBytes = std::size_t{4} << 20U;
inline constexpr std::size_t kDefaultMaxCarveBytes = std::size_t{64} << 20U;

// Bounded-window streaming parameters. Both bounds are OUR configuration,
// never derived from device data (ADR-0009 bounded allocation).
struct ScanConfig {
	std::size_t windowBytes = kDefaultScanWindowBytes;
	std::size_t maxCarveBytes = kDefaultMaxCarveBytes;
};

// Where a scan looks for signatures. This bounds the *search*, not the files
// found by it: a candidate that starts inside the region is carved to its true
// length even when that runs past the end. The boundary belongs to whatever
// claimed the next bytes, not to this file, and truncating there would turn a
// whole recovery into a fragment.
struct ScanRegion {
	std::uint64_t offset;
	std::uint64_t lengthBytes;

	friend bool operator==(const ScanRegion&, const ScanRegion&) = default;
};

struct ScanStats {
	std::uint64_t bytesScanned;
	std::size_t candidateCount;
};

// Streams a BlockDevice in overlapping bounded windows, matches every
// registered signature, runs the owning carver at each hit, and reports
// verdict-carrying candidates. Resumes past Valid/Uncertain extents and one
// byte past Rejected matches. Discovery only — never extraction.
class SignatureScanner {
public:
	SignatureScanner(const CarverRegistry& registry, ScanConfig config) noexcept;

	// The whole device — `scanRegion` over all of it.
	[[nodiscard]] Result<ScanStats> scan(BlockDevice& device, CandidateVisitor& visitor) const;

	[[nodiscard]] Result<ScanStats>
	scanRegion(BlockDevice& device, ScanRegion region, CandidateVisitor& visitor) const;

private:
	// Scratch buffers reused across the whole scan: one window's worth of
	// device bytes, one carve attempt's worth of bytes, and the match list each
	// window refills. Reused rather than returned, so the hot loop allocates
	// nothing beyond the match vector's own growth (ADR-0009).
	struct ScanBuffers {
		std::vector<std::byte> window;
		std::vector<std::byte> carve;
		std::vector<Match> matches;
	};

	// What does not change between windows, carried as one value so no step
	// can pair a region with the wrong device.
	struct ScanContext {
		BlockDevice* device;       // non-owning, never null
		CandidateVisitor* visitor; // non-owning, never null
		ScanRegion region;
	};

	// What one window contributed: where scanning resumes next, and how many
	// candidates it reported (folded into the running ScanStats by the caller).
	struct WindowStep {
		std::uint64_t nextCursor;
		std::size_t candidatesReported;
	};

	// Given this window's already-read matches, reports surviving candidates
	// and computes the next step; terminates on an empty (end-of-device) read.
	// Static: touches only its arguments, no scanner state.
	[[nodiscard]] static Result<WindowStep> stepWindow(
		const ScanContext& context,
		std::uint64_t cursor,
		const WindowMatches& read,
		ScanBuffers& buffers);

	// The slice of the window buffer this step may fill: a whole window, or
	// only what the region still needs plus one signature's reach.
	[[nodiscard]] std::span<std::byte>
	windowSlice(const ScanContext& context, std::uint64_t cursor, ScanBuffers& buffers) const;

	// Reads and processes exactly one window starting at `cursor`.
	[[nodiscard]] Result<WindowStep>
	scanOneWindow(const ScanContext& context, std::uint64_t cursor, ScanBuffers& buffers) const;

	// Runs one window and folds its candidate count into `stats`; returns
	// the next window cursor.
	[[nodiscard]] Result<std::uint64_t> advanceWindow(
		const ScanContext& context,
		std::uint64_t cursor,
		ScanBuffers& buffers,
		ScanStats& stats) const;

	// Drives the window loop across the region.
	[[nodiscard]] Result<ScanStats>
	runScanLoop(const ScanContext& context, ScanBuffers& buffers) const;

	const CarverRegistry* registry_; // non-owning, never null
	ScanConfig config_;
};

} // namespace revenant::carve

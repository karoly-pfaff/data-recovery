// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "revenant/carve/CandidateVisitor.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::carve {

// Defined in src/carve/WindowMatch.hpp — internal scan-loop plumbing, not a
// public interface. Forward-declared here only so the private per-window
// helpers below can take it by (never-dereferenced-in-this-header) reference.
struct WindowMatches;

inline constexpr std::size_t kDefaultScanWindowBytes = std::size_t{4} << 20U;
inline constexpr std::size_t kDefaultMaxCarveBytes = std::size_t{64} << 20U;

// Bounded-window streaming parameters. Both bounds are OUR configuration,
// never derived from device data (ADR-0009 bounded allocation).
struct ScanConfig {
	std::size_t windowBytes = kDefaultScanWindowBytes;
	std::size_t maxCarveBytes = kDefaultMaxCarveBytes;
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

	[[nodiscard]] Result<ScanStats> scan(BlockDevice& device, CandidateVisitor& visitor) const;

private:
	// Scratch buffers reused across the whole scan: one window's worth of
	// device bytes, and one carve attempt's worth of bytes.
	struct ScanBuffers {
		std::vector<std::byte> window;
		std::vector<std::byte> carve;
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
		BlockDevice& device,
		CandidateVisitor& visitor,
		std::uint64_t cursor,
		const WindowMatches& read,
		ScanBuffers& buffers);

	// Reads and processes exactly one window starting at `cursor`.
	[[nodiscard]] Result<WindowStep> scanOneWindow(
		BlockDevice& device,
		CandidateVisitor& visitor,
		std::uint64_t cursor,
		ScanBuffers& buffers) const;

	// Runs one window and folds its candidate count into `stats`; returns
	// the next window cursor.
	[[nodiscard]] Result<std::uint64_t> advanceWindow(
		BlockDevice& device,
		CandidateVisitor& visitor,
		std::uint64_t cursor,
		ScanBuffers& buffers,
		ScanStats& stats) const;

	// Drives the window loop from the device's start to its end.
	[[nodiscard]] Result<ScanStats>
	runScanLoop(BlockDevice& device, CandidateVisitor& visitor, ScanBuffers& buffers) const;

	const CarverRegistry* registry_; // non-owning, never null
	ScanConfig config_;
};

} // namespace revenant::carve

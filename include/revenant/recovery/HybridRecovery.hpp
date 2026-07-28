// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "revenant/carve/CandidateVisitor.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/recovery/ByteAccounting.hpp"

namespace revenant::recovery {

// How much of a volume a run trusts to metadata, and how much to structure.
// Filesystem recovery is precise but fragile; carving is robust but anonymous.
enum class RecoveryMode : std::uint8_t { kFilesystemOnly, kHybrid, kCarveOnly };

// What a run did, in the terms a report and a user care about.
struct RecoveryStats {
	std::uint64_t entriesReported;
	std::uint64_t candidatesReported;
	std::uint64_t accountedBytes;
	std::uint64_t regionsScanned;
	std::uint64_t regionsDropped;
	// False when the volume carried no filesystem this build can read. In
	// hybrid mode that downgrades the run to carving rather than failing it —
	// a formatted or RAW volume is exactly what carving is for — so the fact
	// is reported here rather than swallowed.
	bool filesystemMounted;
};

// Sequences the two recovery sources over one volume: recover what the
// metadata can name, then carve the space those names did not account for.
//
// It owns *sequencing* and nothing else — every parse stays in its own layer,
// and nothing is extracted or written (ADR-0006). The scanner is borrowed and
// must outlive the run.
class HybridRecovery {
public:
	HybridRecovery(const carve::SignatureScanner& scanner, RecoveryMode mode) noexcept;

	[[nodiscard]] Result<RecoveryStats>
	run(BlockDevice& device, fs::EntryVisitor& entries, carve::CandidateVisitor& candidates) const;

private:
	// What the filesystem pass contributed — and whether there was one.
	struct FilesystemPass {
		ByteAccounting accounting;
		std::uint64_t entries = 0;
		bool mounted = false;
	};

	// What the carve pass contributed.
	struct ScanTotals {
		std::uint64_t candidates = 0;
		std::uint64_t regions = 0;
	};

	// A volume that will not mount ends a filesystem-only run and merely
	// downgrades a hybrid one.
	[[nodiscard]] Result<FilesystemPass> mountFailure(Error error) const;

	[[nodiscard]] Result<FilesystemPass>
	runFilesystemPass(BlockDevice& device, fs::EntryVisitor& visitor) const;

	// Where the carve pass looks: nowhere in filesystem-only mode, the whole
	// device when there is no filesystem to trust, and otherwise whatever the
	// confident entries did not account for.
	[[nodiscard]] std::vector<carve::ScanRegion>
	carveRegions(const FilesystemPass& pass, std::uint64_t deviceSize) const;

	// One region scanned, folded into the running totals.
	[[nodiscard]] Result<ScanTotals> scanNextRegion(
		BlockDevice& device,
		carve::ScanRegion region,
		carve::CandidateVisitor& visitor,
		ScanTotals totals) const;

	[[nodiscard]] Result<ScanTotals> scanRegions(
		BlockDevice& device,
		std::span<const carve::ScanRegion> regions,
		carve::CandidateVisitor& visitor) const;

	[[nodiscard]] static RecoveryStats
	statsOf(const FilesystemPass& pass, const ScanTotals& totals);

	const carve::SignatureScanner* scanner_; // non-owning, never null
	RecoveryMode mode_;
};

} // namespace revenant::recovery

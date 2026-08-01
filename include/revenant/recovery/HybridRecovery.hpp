// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "revenant/carve/CandidateVisitor.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/recovery/ByteAccounting.hpp"
#include "revenant/recovery/RunScope.hpp"

namespace revenant::recovery {

// How much of a volume a run trusts to metadata, and how much to structure.
// Filesystem recovery is precise but fragile; carving is robust but anonymous.
enum class RecoveryMode : std::uint8_t { kFilesystemOnly, kHybrid, kCarveOnly };

// How much device the carve pass gets through between progress reports. Bounded
// so an interrupted run loses a bounded amount of work (ADR-0008), whatever the
// shape of the volume: carve-only over a formatted disk is one enormous gap.
inline constexpr std::uint64_t kDefaultCheckpointBytes = std::uint64_t{64} << 20U;

// What a run reports its progress to between chunks, and asks permission of.
// One method, two jobs: it is how a scan gets checkpointed and how it gets
// stopped. The orchestrator owns sequencing; whether to carry on is not its
// decision, and neither files nor signals are its business.
class ScanProgress {
public:
	ScanProgress() = default;
	virtual ~ScanProgress() = default;
	ScanProgress(const ScanProgress&) = delete;
	ScanProgress& operator=(const ScanProgress&) = delete;
	ScanProgress(ScanProgress&&) = delete;
	ScanProgress& operator=(ScanProgress&&) = delete;

	// The device has been scanned up to `cursor`. Returns whether to carry on.
	[[nodiscard]] virtual bool onScanned(std::uint64_t cursor) = 0;
};

// What a run is: which sources it uses, where it picks up, and how often it
// stops to say where it has got to. Every field carries the documented default
// so a plan is never half-initialized; `freshRun` still spells them all out.
struct RecoveryPlan {
	RecoveryMode mode = RecoveryMode::kHybrid;

	// Where the carve pass starts. Nothing means a fresh run. A value means
	// resuming: the filesystem pass then runs for its byte accounting alone,
	// because its entries are already in the index and appending them a second
	// time would make every count after it wrong.
	std::optional<std::uint64_t> resumeFrom = std::nullopt;

	std::uint64_t checkpointBytes = kDefaultCheckpointBytes;
};

// A run of `mode` from the beginning, reporting at the default interval.
[[nodiscard]] RecoveryPlan freshRun(RecoveryMode mode) noexcept;

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
	// True when the mounted filesystem said its own metadata is not what a
	// conforming formatter writes. Recovery went ahead anyway — refusing would
	// throw away files that are plainly there — so the operator is told instead.
	bool nonConformingVolume;
	// False when the run stopped because its progress reporter said to. What
	// it found is real, but arbitration over it would be provisional: the
	// candidate that beats one of these may still be in the unread tail.
	bool scanComplete;
};

// Sequences the two recovery sources over the range a run works in: recover
// what the metadata can name, then carve the space those names did not account
// for.
//
// It owns *sequencing* and nothing else — every parse stays in its own layer,
// and nothing is extracted or written (ADR-0006). The scanner is borrowed and
// must outlive the run.
class HybridRecovery {
public:
	HybridRecovery(const carve::SignatureScanner& scanner, RecoveryPlan plan) noexcept;

	// The scope says where; the plan says how. They are different questions and
	// arrive from different places: the scope was resolved from the source, the
	// plan from what the operator asked for.
	[[nodiscard]] Result<RecoveryStats>
	run(RunScope& scope,
		fs::EntryVisitor& entries,
		carve::CandidateVisitor& candidates,
		ScanProgress& progress) const;

private:
	// What the filesystem pass contributed — and whether there was one.
	struct FilesystemPass {
		ByteAccounting accounting;
		std::uint64_t entries = 0;
		bool mounted = false;
		bool nonConforming = false;
	};

	// What the carve pass contributed.
	struct ScanTotals {
		std::uint64_t candidates = 0;
		std::uint64_t regions = 0;
		bool complete = true;
	};

	// A volume that will not mount ends a filesystem-only run and merely
	// downgrades a hybrid one.
	[[nodiscard]] Result<FilesystemPass> mountFailure(Error error) const;

	// One walk of what the scope decided this run is, with every entry teed into
	// the byte accounting.
	[[nodiscard]] Result<FilesystemPass>
	walkScope(RunScope& scope, fs::EntryVisitor& visitor) const;

	[[nodiscard]] Result<FilesystemPass>
	runFilesystemPass(RunScope& scope, fs::EntryVisitor& visitor) const;

	// Where the carve pass looks: nowhere in filesystem-only mode, the whole
	// device when there is no filesystem to trust, and otherwise whatever the
	// confident entries did not account for — clipped to where a resumed run
	// picks up, and cut into bounded chunks so progress is reported often.
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
		carve::CandidateVisitor& visitor,
		ScanProgress& progress) const;

	// A scan that got through fewer regions than it was handed stopped because
	// its progress reporter said to. What it found is real; what it has not
	// read yet is why arbitration has to wait.
	[[nodiscard]] static Result<ScanTotals>
	stoppedShort(const Result<ScanTotals>& totals, std::size_t regions);

	[[nodiscard]] static RecoveryStats
	statsOf(const FilesystemPass& pass, const ScanTotals& totals);

	const carve::SignatureScanner* scanner_; // non-owning, never null
	RecoveryPlan plan_;
};

} // namespace revenant::recovery

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "revenant/recovery/RecoverySink.hpp"

namespace revenant::cli {

// Whether a run finishes the last of the architecture's three steps or stops
// before it. ADR-0006 separated deciding from writing, so a preview is not a
// mode the engine has to learn — it is the same run, one step shorter.
enum class Delivery : std::uint8_t { kExtract, kPreview };

// What a command line asked for. Listing a source's partitions is not a
// recovery in miniature: it writes nothing, needs nowhere to write, and answers
// the question an operator has *before* they can state a recovery at all.
enum class Action : std::uint8_t { kRecover, kListPartitions };

// One command, as a command line describes it. Every field names something
// `recovery/` or `volume/` already defines: a frontend carries the operator's
// choice of policy, never a policy of its own.
struct RunRequest {
	std::filesystem::path source;
	std::filesystem::path destination;
	std::filesystem::path session;
	recovery::RecoveryMode mode{};
	Delivery delivery{};
	Action action = Action::kRecover;

	// Which partition of the source to work in, numbered as the listing numbers
	// them. Zero is not a partition number, so it is free to mean "the source
	// itself" — an image of one volume, or a whole disk taken as one range.
	std::uint32_t partition = 0;

	// Which formats the carve pass looks for. Empty means every one that ships
	// — the "no filter" default `registerBuiltinCarvers` already documents.
	std::vector<std::string> formats;

	// True when the operator asked for the portable matcher regardless of what
	// the CPU can do. It exists for the machine where the fast path misbehaves:
	// the person whose photographs are on that disk should not have to wait for
	// a release.
	bool forcePortable = false;
};

// What one run did: what it found, what arbitration chose from it, and what
// reached the destination — or, for a preview, what would have.
struct RunReport {
	recovery::RecoveryStats discovery;
	std::uint64_t winners;
	std::uint64_t suppressed;
	recovery::ExtractionStats extraction;
	Delivery delivery;
};

// Runs the architecture's three steps in order — discover, arbitrate, extract
// (ADR-0006) — over the requested source, into its destination.
//
// The source is opened read-only and the destination is validated *before* the
// scan begins: a run that cannot land anywhere should fail in its first second,
// not after an hour of reading. Nothing is written until extraction, which is
// still last.
[[nodiscard]] Result<RunReport> runRecovery(const RunRequest& request);

// A run that could not write down everything it found has already lost the
// answer: the winner set, the suppression count and the output are all derived
// from the index. Failed appends therefore fail the run rather than shrinking
// it silently — the visitors count them precisely so that someone acts on it.
// A scan that already failed keeps its own error, which says more about what
// went wrong than the lost records it caused.
[[nodiscard]] Result<recovery::RecoveryStats>
withoutLostRecords(const Result<recovery::RecoveryStats>& stats, std::uint64_t lostRecords);

} // namespace revenant::cli

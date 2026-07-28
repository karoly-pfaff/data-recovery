// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cli/UndeleteOptions.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "revenant/recovery/RecoverySink.hpp"

namespace revenant::cli {

// What one run did: what it found, what arbitration chose from it, and what
// reached the destination.
struct RunReport {
	recovery::RecoveryStats discovery;
	std::uint64_t winners;
	std::uint64_t suppressed;
	recovery::ExtractionStats extraction;
};

// Runs the architecture's three steps in order — discover, arbitrate, extract
// (ADR-0006) — over the source named in `options`, into its destination.
//
// The source is opened read-only and the destination is validated *before* the
// scan begins: a run that cannot land anywhere should fail in its first second,
// not after an hour of reading. Nothing is written until extraction, which is
// still last.
[[nodiscard]] Result<RunReport> runRecovery(const UndeleteOptions& options);

// A run that could not write down everything it found has already lost the
// answer: the winner set, the suppression count and the output are all derived
// from the index. Failed appends therefore fail the run rather than shrinking
// it silently — the visitors count them precisely so that someone acts on it.
// A scan that already failed keeps its own error, which says more about what
// went wrong than the lost records it caused.
[[nodiscard]] Result<recovery::RecoveryStats>
withoutLostRecords(const Result<recovery::RecoveryStats>& stats, std::uint64_t lostRecords);

} // namespace revenant::cli

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "revenant/core/Confidence.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/recovery/Candidate.hpp"

namespace revenant::recovery {

// What became of one winner on its way to the destination.
enum class ArtifactOutcome : std::uint8_t {
	kWritten,
	// Byte-identical to something already recovered — and anonymous, so the
	// named copy is the one worth keeping.
	kDeduplicated,
	// A name nothing safe survived, a read that came up short, a destination
	// that refused the write. Counted, never hidden.
	kFailed,
};

// One recovered artifact, as the session manifest states it: where the bytes
// came from, what they were called, what they were written as, and whether they
// are the bytes (ADR-0006's discovery record, made durable).
struct ArtifactRecord {
	// The candidate's own name — a volume-relative path, or a carved
	// extension.
	std::string originalName;

	// The destination-relative path actually written, empty when nothing was.
	std::string writtenName;

	std::vector<fs::Extent> extents;
	std::uint64_t bytes;

	// SHA-256 of what was written, as hex. Empty when nothing was.
	std::string contentHash;

	fs::Timestamps timestamps;
	Confidence confidence;
	CandidateSource source;
	ArtifactOutcome outcome;
};

} // namespace revenant::recovery

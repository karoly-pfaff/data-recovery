// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "revenant/core/Confidence.hpp"
#include "revenant/core/io/BadRange.hpp"
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
	// Named but never written: this run stopped before extraction (--dry-run),
	// so the artifact is what *would* have come back.
	kPreviewed,
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

	// The parts of this artifact the device would not give up, handed back as
	// zeros and written out as such — device-absolute, and empty for an artifact
	// that is entirely the device's own bytes.
	//
	// It is a fact about bytes rather than a fourth `Confidence`: validation
	// answers whether the structure holds, and a JPEG whose entropy-coded middle
	// is invented zeros can pass that. `contentHash` still verifies the file that
	// was written; this says how far to trust what was hashed (ADR-0003).
	std::vector<BadRange> invented;

	fs::Timestamps timestamps;
	Confidence confidence;
	CandidateSource source;
	ArtifactOutcome outcome;
};

} // namespace revenant::recovery

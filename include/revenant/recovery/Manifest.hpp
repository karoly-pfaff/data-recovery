// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BadRange.hpp"
#include "revenant/recovery/ArtifactRecord.hpp"
#include "revenant/recovery/HybridRecovery.hpp"

namespace revenant::recovery {

inline constexpr std::string_view kManifestFileName = "manifest.json";

// The durable, machine-readable record of one run: what was recovered, from
// where, and whether the bytes are the bytes. A recovery nobody watched has to
// be auditable afterwards, and this is what makes it so.
//
// Suppressed candidates appear as a count and not as records: `arbitrate`
// reports how many a better explanation displaced, not which ones, because
// holding every loser is the unbounded allocation ADR-0009 forbids.
struct SessionManifest {
	std::filesystem::path source;
	std::filesystem::path destination;
	RecoveryMode mode;
	std::uint64_t winners;
	std::uint64_t suppressed;
	std::vector<ArtifactRecord> artifacts;
	// What the run left its caller to do next, in the words the exit-code table
	// uses. A string and not the frontend's enum: the manifest belongs to
	// `recovery/`, and `cli/` is above it (story-0605).
	std::string outcome;
	// How far the carve pass got, in device bytes. On a run that finished, the
	// end of the last region; on one that stopped, where it stopped.
	std::uint64_t scannedUpTo = 0;
	// The device offset the stop itself names — where a lost source stopped
	// answering. Zero when the stop names none, and on a run that finished.
	std::uint64_t stoppedAt = 0;
	// The run's bad-sector map: every range the device would not give up, taken
	// verbatim from the composed source stack and stated device-absolute.
	//
	// Ranges rather than the bare offsets story-0115 chose. That decision named
	// its own condition — offsets were all a reader that cannot survive a fault
	// could honestly report — and story-0604 retired it by putting a reader that
	// can into every run.
	std::vector<BadRange> unreadable;
};

// Writes `manifest.json` into `sessionDirectory`, next to the candidate index
// (ADR-0008), and returns where it landed. The directory must exist.
[[nodiscard]] Result<std::filesystem::path>
writeManifest(const std::filesystem::path& sessionDirectory, const SessionManifest& manifest);

// The manifest as text, which is what `writeManifest` puts in the file. Exposed
// so the document can be asserted on without going through a filesystem.
[[nodiscard]] std::string manifestJson(const SessionManifest& manifest);

} // namespace revenant::recovery

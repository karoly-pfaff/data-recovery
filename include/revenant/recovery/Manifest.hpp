// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "revenant/core/Result.hpp"
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
	// Device offsets a read stopped at — the bad-sector map, at the resolution
	// a reader that cannot survive a fault can honestly report.
	std::vector<std::uint64_t> unreadable;
};

// Writes `manifest.json` into `sessionDirectory`, next to the candidate index
// (ADR-0008), and returns where it landed. The directory must exist.
[[nodiscard]] Result<std::filesystem::path>
writeManifest(const std::filesystem::path& sessionDirectory, const SessionManifest& manifest);

// The manifest as text, which is what `writeManifest` puts in the file. Exposed
// so the document can be asserted on without going through a filesystem.
[[nodiscard]] std::string manifestJson(const SessionManifest& manifest);

} // namespace revenant::recovery

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include "revenant/core/Result.hpp"
#include "revenant/core/Sha256.hpp"

namespace revenant::recovery {

inline constexpr std::string_view kCheckpointFileName = "checkpoint";

// magic(8) + version(4) + pad(4) + shape(32) + cursor(8) + records(8).
inline constexpr std::size_t kCheckpointBytes = 64;

// How far a run had got when it last stopped to say so (ADR-0008). Small and
// fixed-size on purpose: a checkpoint is replaced by writing a new one beside
// the old and renaming over it, so an interrupted replacement leaves one whole
// checkpoint or the other, never half of each.
struct Checkpoint {
	// What run this belongs to: mode, source size and format allowlist, hashed
	// into one value. A session belonging to a different recovery is rejected
	// whole rather than half-matched, and the record stays fixed-size.
	Sha256Digest shape;

	// The device offset the carve pass has scanned up to.
	std::uint64_t scanCursor;

	// How many candidates the index held when this was written. Anything
	// appended after them describes a region the resumed scan will read again.
	std::uint64_t indexRecords;

	friend bool operator==(const Checkpoint&, const Checkpoint&) = default;
};

[[nodiscard]] Result<std::filesystem::path>
writeCheckpoint(const std::filesystem::path& directory, const Checkpoint& checkpoint);

// Reads the checkpoint in `directory`. A file that is not one of ours, is of
// another version, or is too short is a typed error rather than a guess —
// session state is untrusted on reload like any other bytes (ADR-0009).
[[nodiscard]] Result<Checkpoint> readCheckpoint(const std::filesystem::path& directory);

// Removes any checkpoint in `directory`, so a fresh run cannot be mistaken for
// a resumable one if it stops before writing its first.
void clearCheckpoint(const std::filesystem::path& directory);

} // namespace revenant::recovery

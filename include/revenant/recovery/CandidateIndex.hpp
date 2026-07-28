// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/recovery/Candidate.hpp"

namespace revenant::recovery {

// ADR-0009: an index read back is untrusted data. Every count it states about
// itself is checked against one of these before it sizes anything.
inline constexpr std::size_t kMaxCandidateNameBytes = 4096;
inline constexpr std::size_t kMaxCandidateExtents = 4096;
inline constexpr std::size_t kMaxResidentBytes = std::size_t{1} << 16U;

// Appending is unbounded — that is what makes the index file-backed and what
// lets a terabyte scan finish. Reading it *back* materializes candidates, so
// that side is capped; external sorting, if a real device ever needs it, is a
// later story rather than machinery invented here.
inline constexpr std::size_t kMaxIndexedCandidates = std::size_t{1} << 20U;

// What reading an index produced: its candidates in append order, and how many
// records were dropped for being torn or impossible.
struct IndexContents {
	std::vector<Candidate> candidates;
	std::uint64_t droppedRecords;
};

// The durable record of everything a run discovered, written into the session
// directory — never onto the source (ADR-0005).
//
// Two append-only files: fixed-size records, and a blob holding the
// variable-length parts. The blob is written *before* the record that points
// at it, so a record can never refer to bytes that are not on disk; an
// interrupted run therefore leaves a readable prefix rather than a corrupt
// file (ADR-0008).
class CandidateIndex {
public:
	// Starts a new, empty index in `directory`, replacing anything already
	// there. The directory must exist.
	[[nodiscard]] static Result<CandidateIndex> create(const std::filesystem::path& directory);

	// Appends one candidate; returns its ordinal in append order.
	[[nodiscard]] Result<std::uint64_t> append(const Candidate& candidate);

	[[nodiscard]] std::uint64_t count() const noexcept;

private:
	CandidateIndex(std::ofstream records, std::ofstream blob);

	[[nodiscard]] Result<std::uint64_t> writeEntry(const Candidate& candidate);

	std::ofstream records_;
	std::ofstream blob_;
	std::uint64_t blobBytes_ = 0;
	std::uint64_t count_ = 0;
};

// Reads back everything an index holds, in append order. A record that is torn
// (the file's record area is not a whole multiple of the record size), that
// points past the blob, or that states a count past one of the bounds above is
// dropped and counted — never half-interpreted. A file that is not an index of
// this version is a typed error, not a guess.
[[nodiscard]] Result<IndexContents> readIndex(const std::filesystem::path& directory);

} // namespace revenant::recovery

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <string>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/recovery/Candidate.hpp"

namespace revenant::recovery {

// How much of a file the sink holds in memory while copying it. A winner's
// extents come off a disk, so the copy is chunked rather than sized by them
// (ADR-0009): a 4 GiB video does not become a 4 GiB allocation.
inline constexpr std::size_t kExtractChunkBytes = std::size_t{1} << 20U;

// What an extraction run did.
struct ExtractionStats {
	std::uint64_t filesWritten;
	std::uint64_t bytesWritten;
	// Winners that could not be written: a name nothing safe survived, a read
	// that came up short, a destination that refused the write. Counted rather
	// than hidden — a recovery tool has to be able to say what it could not
	// get back.
	std::uint64_t failed;
	// Winners written under a different name because one was already taken.
	// A rename is a fact about the output, so it is reported, not silent.
	std::uint64_t renamed;
};

// The one place in the project that creates files. Everything before it is
// discovery (ADR-0006); the source device is only ever read (ADR-0005), and
// every output path passes through `sanitizeOutputPath` (ADR-0009) — there is
// no other way to derive one.
class RecoverySink {
public:
	// Validates the destination once: it must exist, be a directory, and not
	// contain the source. Recovered data must not be written onto the media
	// being recovered.
	[[nodiscard]] static Result<RecoverySink>
	open(const std::filesystem::path& destination, const std::filesystem::path& source);

	// Writes every winner, in the order given; the ordinal a carved winner is
	// named after is its position in that order.
	[[nodiscard]] ExtractionStats extract(std::span<const Candidate> winners, BlockDevice& device);

private:
	explicit RecoverySink(std::filesystem::path destination);

	// The bytes written, or the reason nothing was.
	[[nodiscard]] Result<std::uint64_t>
	write(const Candidate& winner, BlockDevice& device, std::uint64_t ordinal);

	// The destination-relative name to use, after collision suffixing, or
	// nothing when the proposed name has no safe form at all.
	[[nodiscard]] std::optional<std::string>
	claimName(const Candidate& winner, std::uint64_t ordinal);

	void record(const Result<std::uint64_t>& written);

	std::filesystem::path destination_;
	std::set<std::string> used_;
	ExtractionStats stats_{.filesWritten = 0, .bytesWritten = 0, .failed = 0, .renamed = 0};
};

} // namespace revenant::recovery

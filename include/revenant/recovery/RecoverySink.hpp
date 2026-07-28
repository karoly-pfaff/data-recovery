// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/Sha256.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/recovery/ArtifactRecord.hpp"
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
	// Carved winners dropped for holding bytes already recovered under a name.
	std::uint64_t deduplicated;
};

// Everything an extraction produced: the totals, the per-artifact record the
// manifest is written from, and where reading the source failed.
struct Extraction {
	ExtractionStats stats;
	std::vector<ArtifactRecord> artifacts;
	// Device offsets a read stopped at. Offsets and not ranges: bounding the
	// damage needs a reader that survives the fault and probes forward, which
	// is imaging mode (M4). Stating a length this build cannot know would make
	// the manifest confidently wrong.
	std::vector<std::uint64_t> unreadable;
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

	// Writes every winner. Named artifacts go first so a carved duplicate of a
	// named recovery always arrives second and loses; the ordinal a carved
	// winner is named after is still its position in the order given, so the
	// names on disk do not depend on the order they were written in.
	[[nodiscard]] Extraction extract(std::span<const Candidate> winners, BlockDevice& device);

	// Every winner as it *would* be written — the same names, the same order,
	// the same collision renames — with nothing created. ADR-0006 already
	// separated deciding from writing, so a preview is not a mode: it is
	// stopping before the last step.
	//
	// Nothing is read, so no artifact carries a hash or a size: a digest costs
	// a full read of every artifact, which is most of what extraction is, and
	// a preview that read everything would not be a preview.
	[[nodiscard]] Extraction preview(std::span<const Candidate> winners);

private:
	explicit RecoverySink(std::filesystem::path destination);

	// One winner written: where it landed, and what turned out to be in it.
	struct WrittenFile {
		std::string name;
		std::filesystem::path target;
		std::uint64_t bytes;
		Sha256Digest content;
	};

	[[nodiscard]] Result<WrittenFile>
	write(const Candidate& winner, BlockDevice& device, std::uint64_t ordinal);

	// The destination-relative name to use, after collision suffixing, or
	// nothing when the proposed name has no safe form at all.
	[[nodiscard]] std::optional<std::string>
	claimName(const Candidate& winner, std::uint64_t ordinal);

	void record(const Candidate& winner, const Result<WrittenFile>& written);

	// A carved artifact holding bytes already recovered is removed again: what
	// it duplicates already has a name, and nothing can know it is a duplicate
	// until its last byte has been hashed.
	[[nodiscard]] bool dropIfDuplicate(const Candidate& winner, const WrittenFile& written);

	// The artifact stays: its digest is claimed and its record is written.
	void keep(const Candidate& winner, const WrittenFile& written);

	// One winner named but not written, and recorded as such.
	void previewOne(const Candidate& winner, std::uint64_t ordinal);

	std::filesystem::path destination_;
	std::set<std::string> used_;
	std::set<Sha256Digest> written_;
	Extraction result_{
		.stats =
			ExtractionStats{
				.filesWritten = 0,
				.bytesWritten = 0,
				.failed = 0,
				.renamed = 0,
				.deduplicated = 0},
		.artifacts = {},
		.unreadable = {}};
};

} // namespace revenant::recovery

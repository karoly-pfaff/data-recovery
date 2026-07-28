// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/RecoverySink.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "recovery/ExtractFile.hpp"
#include "recovery/WriteOrder.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/Sha256.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/recovery/ArtifactRecord.hpp"
#include "revenant/recovery/Candidate.hpp"
#include "revenant/recovery/Disambiguate.hpp"
#include "revenant/recovery/OutputName.hpp"
#include "revenant/recovery/OutputPath.hpp"

namespace revenant::recovery {

namespace {

// Recovered data must not be written onto the media being recovered, so a
// destination that contains the source is refused outright (ADR-0005). Both
// sides are canonicalized first: two spellings of one directory are one
// directory.
[[nodiscard]] bool
contains(const std::filesystem::path& outer, const std::filesystem::path& inner) {
	std::error_code failed;
	const auto root = std::filesystem::weakly_canonical(outer, failed);
	const auto candidate = std::filesystem::weakly_canonical(inner, failed);
	const auto reach = std::ranges::mismatch(root, candidate);
	return reach.in1 == root.end();
}

[[nodiscard]] bool destinationIsUsable(const std::filesystem::path& destination) {
	std::error_code failed;
	return std::filesystem::is_directory(destination, failed);
}

// The shape of every record, filled in by whichever outcome produced it.
[[nodiscard]] ArtifactRecord recordFor(const Candidate& winner, ArtifactOutcome outcome) {
	return ArtifactRecord{
		.originalName = winner.name,
		.writtenName = {},
		.extents = winner.extents,
		.bytes = 0,
		.contentHash = {},
		.timestamps = winner.timestamps,
		.confidence = winner.confidence,
		.source = winner.source,
		.outcome = outcome};
}

} // namespace

RecoverySink::RecoverySink(std::filesystem::path destination)
	: destination_(std::move(destination)) {}

Result<RecoverySink>
RecoverySink::open(const std::filesystem::path& destination, const std::filesystem::path& source) {
	if (!destinationIsUsable(destination)) {
		return Error{.code = ErrorCode::kNotFound};
	}
	if (contains(destination, source)) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	return RecoverySink{destination};
}

std::optional<std::string> RecoverySink::claimName(const Candidate& winner, std::uint64_t ordinal) {
	const auto proposed = outputNameFor(winner, ordinal);
	if (!sanitizeOutputPath(destination_, proposed).hasValue()) {
		return std::nullopt;
	}
	auto claimed = disambiguate(proposed, [this](std::string_view name) {
		return used_.contains(std::string{name});
	});
	result_.stats.renamed += claimed == proposed ? 0U : 1U;
	used_.insert(claimed);
	return claimed;
}

Result<RecoverySink::WrittenFile>
RecoverySink::write(const Candidate& winner, BlockDevice& device, std::uint64_t ordinal) {
	const auto claimed = claimName(winner, ordinal);
	if (!claimed.has_value()) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	const auto target = sanitizeOutputPath(destination_, claimed.value());
	if (!target.hasValue()) {
		return target.error();
	}
	return extractTo(target.value(), winner, device).map([&](const ExtractedFile& extracted) {
		return WrittenFile{
			.name = claimed.value(),
			.target = target.value(),
			.bytes = extracted.bytes,
			.content = extracted.content};
	});
}

bool RecoverySink::dropIfDuplicate(const Candidate& winner, const WrittenFile& written) {
	if (winner.source != CandidateSource::kCarve || !written_.contains(written.content)) {
		return false;
	}
	std::error_code ignored;
	std::filesystem::remove(written.target, ignored);
	++result_.stats.deduplicated;
	result_.artifacts.push_back(recordFor(winner, ArtifactOutcome::kDeduplicated));
	return true;
}

void RecoverySink::record(const Candidate& winner, const Result<WrittenFile>& written) {
	if (!written.hasValue()) {
		++result_.stats.failed;
		result_.unreadable.push_back(written.error().offset);
		result_.artifacts.push_back(recordFor(winner, ArtifactOutcome::kFailed));
		return;
	}
	if (dropIfDuplicate(winner, written.value())) {
		return;
	}
	keep(winner, written.value());
}

void RecoverySink::keep(const Candidate& winner, const WrittenFile& written) {
	written_.insert(written.content);
	++result_.stats.filesWritten;
	result_.stats.bytesWritten += written.bytes;
	ArtifactRecord record = recordFor(winner, ArtifactOutcome::kWritten);
	record.writtenName = written.name;
	record.bytes = written.bytes;
	record.contentHash = toHex(written.content);
	result_.artifacts.push_back(std::move(record));
}

Extraction RecoverySink::extract(std::span<const Candidate> winners, BlockDevice& device) {
	for (const Ordered& item : orderedForWriting(winners)) {
		record(*item.winner, write(*item.winner, device, item.ordinal));
	}
	return std::move(result_);
}

} // namespace revenant::recovery

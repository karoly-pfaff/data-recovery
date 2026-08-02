// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/RecoverySink.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "recovery/DestinationRule.hpp"
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
		// Filled where the run's bad-sector map meets the finished extraction;
		// the sink never sees a fault, because the stack below it absorbs one.
		.invented = {},
		.timestamps = winner.timestamps,
		.confidence = winner.confidence,
		.source = winner.source,
		.outcome = outcome};
}

// The winners a stop never reached. Not a failure of theirs — the next run over
// the same destination writes them — but a manifest that omitted them would
// claim a smaller world than the run decided on.
void recordNotAttempted(Extraction& result, std::span<const Ordered> ordered) {
	for (const Ordered& item : ordered) {
		result.artifacts.push_back(recordFor(*item.winner, ArtifactOutcome::kNotAttempted));
	}
}

// Which write failures make every further write futile, as against the ones
// that cost this artifact and no other. A destination with no room left will
// refuse the next winner too, and so will a source that has gone away — reading
// is half of writing a recovered file (story-0605).
[[nodiscard]] bool endsTheRun(ErrorCode code) noexcept {
	return code == ErrorCode::kStorageExhausted || code == ErrorCode::kSourceLost;
}

} // namespace

RecoverySink::RecoverySink(std::filesystem::path destination)
	: destination_(std::move(destination)) {}

Result<RecoverySink>
RecoverySink::open(const std::filesystem::path& destination, const std::filesystem::path& source) {
	if (!destinationIsUsable(destination)) {
		return Error{.code = ErrorCode::kNotFound};
	}
	if (const auto refusal = destinationOnSource(destination, source)) {
		return refusal.value();
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

bool RecoverySink::writeOne(const Candidate& winner, BlockDevice& device, std::uint64_t ordinal) {
	const auto written = write(winner, device, ordinal);
	record(winner, written);
	if (written.hasValue() || !endsTheRun(written.error().code)) {
		return true;
	}
	result_.stoppedBy = written.error();
	return false;
}

// Every winner in turn until one of them says the run cannot go on, and then
// the ones its turn never came for.
void RecoverySink::writeEveryWinner(std::span<const Candidate> winners, BlockDevice& device) {
	const auto ordered = orderedForWriting(winners);
	std::size_t done = 0;
	for (const Ordered& item : ordered) {
		++done;
		if (!writeOne(*item.winner, device, item.ordinal)) {
			break;
		}
	}
	recordNotAttempted(result_, std::span{ordered}.subspan(done));
}

Extraction RecoverySink::extract(std::span<const Candidate> winners, BlockDevice& device) {
	writeEveryWinner(winners, device);
	return std::move(result_);
}

void RecoverySink::previewOne(const Candidate& winner, std::uint64_t ordinal) {
	const auto claimed = claimName(winner, ordinal);
	if (!claimed.has_value()) {
		++result_.stats.failed;
		result_.artifacts.push_back(recordFor(winner, ArtifactOutcome::kFailed));
		return;
	}
	ArtifactRecord record = recordFor(winner, ArtifactOutcome::kPreviewed);
	record.writtenName = claimed.value();
	result_.artifacts.push_back(std::move(record));
}

Extraction RecoverySink::preview(std::span<const Candidate> winners) {
	for (const Ordered& item : orderedForWriting(winners)) {
		previewOne(*item.winner, item.ordinal);
	}
	return std::move(result_);
}

} // namespace revenant::recovery

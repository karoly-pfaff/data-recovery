// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/RunSummary.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cli/UndeleteRun.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "revenant/recovery/RecoverySink.hpp"

namespace revenant::cli {

namespace {

// `label value`, the one shape every count in the summary takes. Labelled
// rather than pluralized: "files 1" reads the same as "files 12", and a run's
// output is as often grepped as read.
[[nodiscard]] std::string field(std::string_view label, std::uint64_t value) {
	return std::string{label} + " " + std::to_string(value);
}

[[nodiscard]] std::string mountNote(bool filesystemMounted) {
	if (filesystemMounted) {
		return {};
	}
	return " (no readable filesystem; carved the whole device)";
}

[[nodiscard]] std::string discoveryLine(const recovery::RecoveryStats& stats) {
	return "discovery: " + field("filesystem entries", stats.entriesReported) + ", " +
		   field("carve candidates", stats.candidatesReported) + ", " +
		   field("regions scanned", stats.regionsScanned) + mountNote(stats.filesystemMounted);
}

[[nodiscard]] std::string arbitrationLine(const RunReport& report) {
	return "arbitration: " + field("winners", report.winners) + ", " +
		   field("suppressed", report.suppressed);
}

[[nodiscard]] std::string extractionLine(const recovery::ExtractionStats& stats) {
	return "extraction: " + field("files", stats.filesWritten) + ", " +
		   field("bytes", stats.bytesWritten) + ", " + field("failed", stats.failed) + ", " +
		   field("renamed", stats.renamed);
}

} // namespace

std::vector<std::string> summarize(const RunReport& report) {
	return {
		discoveryLine(report.discovery),
		arbitrationLine(report),
		extractionLine(report.extraction)};
}

std::string describe(const Error& error) {
	switch (error.code) {
	case ErrorCode::kNotFound:
		return "a required path does not exist; check --source and --destination";
	case ErrorCode::kInvalidArgument:
		return "the destination must exist, be a directory, and not contain the source";
	case ErrorCode::kIoFailure:
		return "a read or write failed; the run stopped rather than report a smaller world";
	case ErrorCode::kOutOfRange:
		return "a read ran past the end of what it was given";
	case ErrorCode::kOverflow:
		return "an offset or length calculation would have overflowed";
	}
	return "the run failed for an unrecorded reason";
}

} // namespace revenant::cli

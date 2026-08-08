// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/RunSummary.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cli/RecoveryRun.hpp"
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

// A volume whose own metadata is not what a conforming formatter writes was
// still read — refusing it would throw away files that are plainly there — so
// the operator is told rather than protected from it.
[[nodiscard]] std::string conformanceNote(bool nonConforming) {
	if (!nonConforming) {
		return {};
	}
	return " (warning: the volume's metadata is not what a conforming formatter"
		   " writes; recovery is best-effort)";
}

[[nodiscard]] std::string discoveryLine(const recovery::RecoveryStats& stats) {
	return "discovery: " + field("filesystem entries", stats.entriesReported) + ", " +
		   field("carve candidates", stats.candidatesReported) + ", " +
		   field("regions scanned", stats.regionsScanned) + mountNote(stats.filesystemMounted) +
		   conformanceNote(stats.nonConformingVolume);
}

[[nodiscard]] std::string arbitrationLine(const RunReport& report) {
	return "arbitration: " + field("winners", report.winners) + ", " +
		   field("suppressed", report.suppressed);
}

[[nodiscard]] std::string extractionLine(const recovery::ExtractionStats& stats) {
	return "extraction: " + field("files", stats.filesWritten) + ", " +
		   field("bytes", stats.bytesWritten) + ", " + field("failed", stats.failed) + ", " +
		   field("renamed", stats.renamed) + ", " + field("deduplicated", stats.deduplicated);
}

// A preview reports only what it can know: it named every winner and wrote
// none of them, so there are no bytes and no digests to speak of.
[[nodiscard]] std::string previewLine(const RunReport& report) {
	return "preview: " + field("artifacts", report.winners - report.extraction.failed) + ", " +
		   field("unusable", report.extraction.failed) + ", " +
		   field("renamed", report.extraction.renamed) + " (nothing was written)";
}

// What the device would not give up, and how many artifacts sit on some of it.
// The line appears only when there is damage, so an undamaged run's output is
// unchanged — and a damaged one can never be mistaken for it.
//
// The sentence says what happened to the *bytes*, not what happened to a file,
// because this line is printed by an extraction, a preview and an interrupted
// run alike, and only the first of those wrote anything. "Invented" is the word
// on purpose: those bytes came from this tool rather than from the disk.
[[nodiscard]] std::string damageLine(const RunReport& report) {
	return "damage: " + field("unreadable bytes", report.unreadableBytes) + ", " +
		   field("artifacts with invented bytes", report.extraction.degraded) +
		   " (the device refused these sectors and they were read as zeros;"
		   " see `invented` in the manifest for what they fall inside)";
}

// An interrupted run decided nothing and wrote nothing, on purpose: arbitrating
// a partial index can crown a winner the finished scan would have suppressed.
[[nodiscard]] std::string incompleteLine() {
	return "incomplete: the scan was interrupted; nothing was decided or written."
		   " Re-run with the same destination to carry on";
}

[[nodiscard]] std::string deliveryLine(const RunReport& report) {
	if (!report.discovery.scanComplete) {
		return incompleteLine();
	}
	if (report.delivery == Delivery::kPreview) {
		return previewLine(report);
	}
	return extractionLine(report.extraction);
}

// The failures that stop a run before it starts: every one of them is about
// what the run was pointed at, and every one is fixed by changing an argument.
// Empty means the code is not one of these, which `describe` reads as "ask the
// other half" — the enum is listed exhaustively on both sides so that adding a
// code is a compile error until it is given a sentence.
[[nodiscard]] std::string beforeTheRun(ErrorCode code) {
	switch (code) {
	case ErrorCode::kNotFound:
		return "a required path does not exist; check --source and --destination";
	case ErrorCode::kInvalidArgument:
		return "the destination must exist, be a directory, and not be a folder the source"
			   " sits inside";
	case ErrorCode::kDestinationOnSource:
		return "the destination is on the storage being recovered; writing there would"
			   " overwrite the very clusters the run reads."
			   " Point --destination at a different physical disk";
	case ErrorCode::kDestinationIdentityUnresolved:
		return "the source has no physical identity this tool can resolve — an encrypted"
			   " volume is the usual reason — so it cannot confirm the destination is not on"
			   " it. It is not saying the destination is unsafe; it is saying it cannot tell."
			   " --allow-unverified-destination exists for an operator who has confirmed that"
			   " themselves";
	case ErrorCode::kNotBlockAddressable:
		return "the source is a folder, and a folder holds only the files that are still"
			   " there; recovery reads the bytes underneath a filesystem, so point --source"
			   " at a disk image or a device (an image *on* a network share is fine)";
	case ErrorCode::kPermissionDenied:
		return "the operating system refused to open the source: reading a whole disk or a"
			   " mounted volume needs administrator (Windows) or root/disk-group (Linux)"
			   " privilege";
	case ErrorCode::kIoFailure:
	case ErrorCode::kOutOfRange:
	case ErrorCode::kOverflow:
	case ErrorCode::kSourceLost:
	case ErrorCode::kStorageExhausted:
		break;
	}
	return {};
}

// What went wrong once it was running. An operator cannot argue with any of
// these by re-spelling a flag.
[[nodiscard]] std::string duringTheRun(ErrorCode code) {
	switch (code) {
	case ErrorCode::kIoFailure:
		return "a read or write failed; the run stopped rather than report a smaller world";
	case ErrorCode::kSourceLost:
		return "the source stopped answering: a megabyte of it in a row would not read, which"
			   " is a device that has gone rather than a patch of bad sectors. Everything"
			   " recovered so far is written and accounted for; re-run the same command to"
			   " carry on from where the scan stopped";
	case ErrorCode::kStorageExhausted:
		return "the destination or the session directory has no room left. What was written"
			   " stays; free space or point --destination somewhere else, then re-run the same"
			   " command";
	case ErrorCode::kOutOfRange:
		return "a read ran past the end of what it was given";
	case ErrorCode::kOverflow:
		return "an offset or length calculation would have overflowed";
	case ErrorCode::kNotFound:
	case ErrorCode::kInvalidArgument:
	case ErrorCode::kDestinationOnSource:
	case ErrorCode::kDestinationIdentityUnresolved:
	case ErrorCode::kNotBlockAddressable:
	case ErrorCode::kPermissionDenied:
		break;
	}
	return "the run failed for an unrecorded reason";
}

} // namespace

std::vector<std::string> summarize(const RunReport& report) {
	std::vector<std::string> lines{
		discoveryLine(report.discovery),
		arbitrationLine(report),
		deliveryLine(report)};
	if (report.unreadableBytes > 0) {
		lines.push_back(damageLine(report));
	}
	return lines;
}

std::string describe(const Error& error) {
	const auto stopped = beforeTheRun(error.code);
	return stopped.empty() ? duringTheRun(error.code) : stopped;
}

} // namespace revenant::cli

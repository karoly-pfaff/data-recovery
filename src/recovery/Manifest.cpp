// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/Manifest.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "recovery/ManifestJson.hpp"
#include "recovery/SessionFile.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BadRange.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/recovery/ArtifactRecord.hpp"
#include "revenant/recovery/Candidate.hpp"
#include "revenant/recovery/HybridRecovery.hpp"

namespace revenant::recovery {

namespace {

// The manifest names the mode the way the command line does, so a run can be
// read back and repeated.
[[nodiscard]] std::string_view nameOf(RecoveryMode mode) {
	switch (mode) {
	case RecoveryMode::kFilesystemOnly:
		return "fs-only";
	case RecoveryMode::kHybrid:
		return "hybrid";
	case RecoveryMode::kCarveOnly:
		return "carve-only";
	}
	return "unknown";
}

[[nodiscard]] std::string_view nameOf(CandidateSource source) {
	return source == CandidateSource::kFilesystem ? "filesystem" : "carve";
}

[[nodiscard]] std::string_view nameOf(Confidence confidence) {
	switch (confidence) {
	case Confidence::kValid:
		return "valid";
	case Confidence::kUncertain:
		return "uncertain";
	case Confidence::kRejected:
		return "rejected";
	}
	return "unknown";
}

[[nodiscard]] std::string_view nameOf(ArtifactOutcome outcome) {
	switch (outcome) {
	case ArtifactOutcome::kWritten:
		return "written";
	case ArtifactOutcome::kDeduplicated:
		return "deduplicated";
	case ArtifactOutcome::kFailed:
		return "failed";
	case ArtifactOutcome::kPreviewed:
		return "previewed";
	case ArtifactOutcome::kNotAttempted:
		return "not-attempted";
	}
	return "unknown";
}

// What a file occupies and what could not be read are both one {offset, length}
// pair, and the manifest spells them the same way on purpose: an operator
// comparing the two should not have to translate between two shapes — nor
// between two origins, which is why every offset in this document counts from
// the start of the source device, including a scoped run's extents.
[[nodiscard]] std::string rangeJson(std::uint64_t offset, std::uint64_t length) {
	const std::vector<std::string> members{
		json::member("offset", offset),
		json::member("length", length)};
	return json::object(members);
}

[[nodiscard]] std::string rangeJson(const fs::Extent& extent) {
	return rangeJson(extent.deviceOffset, extent.lengthBytes);
}

[[nodiscard]] std::string rangeJson(const BadRange& range) {
	return rangeJson(range.offsetBytes, range.lengthBytes);
}

template <typename Range> [[nodiscard]] std::string rangesJson(const std::vector<Range>& ranges) {
	std::vector<std::string> items;
	items.reserve(ranges.size());
	for (const Range& one : ranges) {
		items.push_back(rangeJson(one));
	}
	return json::array(items);
}

[[nodiscard]] std::string timestampsJson(const fs::Timestamps& timestamps) {
	const std::vector<std::string> members{
		json::member("created", timestamps.created),
		json::member("modified", timestamps.modified),
		json::member("accessed", timestamps.accessed)};
	return json::object(members);
}

[[nodiscard]] std::string artifactJson(const ArtifactRecord& artifact) {
	const std::vector<std::string> members{
		json::member("originalName", artifact.originalName),
		json::member("writtenName", artifact.writtenName),
		json::member("source", nameOf(artifact.source)),
		json::member("confidence", nameOf(artifact.confidence)),
		json::member("outcome", nameOf(artifact.outcome)),
		json::member("bytes", artifact.bytes),
		json::member("sha256", artifact.contentHash),
		json::rawMember("extents", rangesJson(artifact.extents)),
		json::rawMember("invented", rangesJson(artifact.invented)),
		json::rawMember("timestamps", timestampsJson(artifact.timestamps))};
	return json::object(members);
}

[[nodiscard]] std::string artifactsJson(const std::vector<ArtifactRecord>& artifacts) {
	std::vector<std::string> items;
	items.reserve(artifacts.size());
	for (const ArtifactRecord& artifact : artifacts) {
		items.push_back(artifactJson(artifact));
	}
	return json::array(items);
}

[[nodiscard]] std::vector<std::string> runMembers(const SessionManifest& manifest) {
	return {
		json::rawMember("source", json::quotedPath(manifest.source)),
		json::rawMember("destination", json::quotedPath(manifest.destination)),
		json::member("mode", nameOf(manifest.mode)),
		json::member("winners", manifest.winners),
		json::member("suppressed", manifest.suppressed),
		json::member("outcome", manifest.outcome),
		json::member("scannedUpTo", manifest.scannedUpTo),
		json::member("stoppedAt", manifest.stoppedAt),
		json::rawMember("unreadable", rangesJson(manifest.unreadable)),
		json::rawMember("artifacts", artifactsJson(manifest.artifacts))};
}

} // namespace

std::string manifestJson(const SessionManifest& manifest) {
	return json::object(runMembers(manifest));
}

Result<std::filesystem::path>
writeManifest(const std::filesystem::path& sessionDirectory, const SessionManifest& manifest) {
	const auto text = manifestJson(manifest);
	return replaceFile(sessionDirectory, kManifestFileName, std::as_bytes(std::span{text}));
}

} // namespace revenant::recovery

// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/Manifest.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <vector>

#include "recovery/ManifestJson.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
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
	}
	return "unknown";
}

[[nodiscard]] std::string extentJson(const fs::Extent& extent) {
	const std::vector<std::string> members{
		json::member("offset", extent.deviceOffset),
		json::member("length", extent.lengthBytes)};
	return json::object(members);
}

[[nodiscard]] std::string extentsJson(const std::vector<fs::Extent>& extents) {
	std::vector<std::string> items;
	items.reserve(extents.size());
	for (const fs::Extent& extent : extents) {
		items.push_back(extentJson(extent));
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
		json::rawMember("extents", extentsJson(artifact.extents)),
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

[[nodiscard]] std::string offsetsJson(const std::vector<std::uint64_t>& offsets) {
	std::vector<std::string> items;
	items.reserve(offsets.size());
	for (const std::uint64_t offset : offsets) {
		items.push_back(std::to_string(offset));
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
		json::rawMember("unreadable", offsetsJson(manifest.unreadable)),
		json::rawMember("artifacts", artifactsJson(manifest.artifacts))};
}

} // namespace

std::string manifestJson(const SessionManifest& manifest) {
	return json::object(runMembers(manifest));
}

Result<std::filesystem::path>
writeManifest(const std::filesystem::path& sessionDirectory, const SessionManifest& manifest) {
	const auto target = sessionDirectory / kManifestFileName;
	std::ofstream file{target, std::ios::binary | std::ios::trunc};
	file << manifestJson(manifest);
	file.flush();
	if (!file.good()) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	return target;
}

} // namespace revenant::recovery

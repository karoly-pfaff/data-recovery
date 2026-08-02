// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/Session.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "cli/Interrupt.hpp"
#include "cli/RecoveryRun.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/Sha256.hpp"
#include "revenant/recovery/CandidateIndex.hpp"
#include "recovery/StorageRoom.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/recovery/Checkpoint.hpp"

namespace revenant::cli {

namespace {

// Everything that decides *what* a run scans, written out so the digest of it
// changes whenever any of it does.
[[nodiscard]] std::string descriptionOf(const RunRequest& request, std::uint64_t sourceSize) {
	std::string description =
		std::to_string(static_cast<unsigned>(request.mode)) + ":" + std::to_string(sourceSize);
	for (const std::string& format : request.formats) {
		description += ":" + format;
	}
	return description;
}

[[nodiscard]] Result<OpenSession> freshSession(const std::filesystem::path& session) {
	recovery::clearCheckpoint(session);
	auto index = recovery::CandidateIndex::create(session);
	if (!index.hasValue()) {
		return index.error();
	}
	return OpenSession{.index = std::move(index.value()), .resumeFrom = std::nullopt};
}

[[nodiscard]] Result<OpenSession>
continuedSession(const std::filesystem::path& session, const recovery::Checkpoint& from) {
	auto index = recovery::CandidateIndex::reopen(session, from.indexRecords);
	if (!index.hasValue()) {
		return freshSession(session);
	}
	return OpenSession{.index = std::move(index.value()), .resumeFrom = from.scanCursor};
}

// The checkpoint worth resuming from, or nothing when there is none, it cannot
// be read, or it belongs to another run.
[[nodiscard]] std::optional<recovery::Checkpoint>
resumable(const RunRequest& request, std::uint64_t sourceSize) {
	const auto stored = recovery::readCheckpoint(request.session);
	if (!stored.hasValue() || stored.value().shape != shapeOf(request, sourceSize)) {
		return std::nullopt;
	}
	return stored.value();
}

} // namespace

Sha256Digest shapeOf(const RunRequest& request, std::uint64_t sourceSize) {
	const auto description = descriptionOf(request, sourceSize);
	return sha256(std::as_bytes(std::span{description}));
}

Result<OpenSession> openSession(const RunRequest& request, std::uint64_t sourceSize) {
	const auto stored = resumable(request, sourceSize);
	if (!stored.has_value()) {
		return freshSession(request.session);
	}
	return continuedSession(request.session, stored.value());
}

Checkpointer::Checkpointer(
	std::filesystem::path session,
	const Sha256Digest& shape,
	const recovery::CandidateIndex& index) noexcept
	: session_(std::move(session)), shape_(shape), index_(&index) {}

bool Checkpointer::onScanned(std::uint64_t cursor) {
	const auto written = recovery::writeCheckpoint(
		session_,
		recovery::Checkpoint{
			.shape = shape_,
			.scanCursor = cursor,
			.indexRecords = index_->count()});
	if (!written.hasValue()) {
		unwritable_ = Error{
			.code = recovery::writeFailureAt(session_),
			.offset = cursor,
			.osCode = written.error().osCode};
		return false;
	}
	return !interrupted();
}

const std::optional<Error>& Checkpointer::unwritable() const noexcept {
	return unwritable_;
}

} // namespace revenant::cli

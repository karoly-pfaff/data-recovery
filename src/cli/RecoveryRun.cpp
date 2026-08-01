// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/RecoveryRun.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "cli/RunDelivery.hpp"
#include "cli/Session.hpp"
#include "revenant/carve/BuiltinCarvers.hpp"
#include "revenant/carve/CandidateVisitor.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/carve/SignatureTable.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/core/io/SourceDevice.hpp"
#include "revenant/core/io/SourceStack.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "revenant/recovery/IndexingVisitors.hpp"
#include "revenant/recovery/RecoverySink.hpp"
#include "revenant/recovery/RunScope.hpp"

namespace revenant::cli {

namespace {

[[nodiscard]] std::vector<std::string_view> viewsOf(const std::vector<std::string>& names) {
	return {names.begin(), names.end()};
}

[[nodiscard]] carve::MatchPath matchPathFor(bool forcePortable) noexcept {
	return forcePortable ? carve::MatchPath::kPortableOnly : carve::MatchPath::kAuto;
}

// The carvers this run searches for, and how it matches them. An empty
// allowlist registers every format that ships, so "no filter" is the default
// rather than "nothing works".
[[nodiscard]] carve::CarverRegistry registryFor(const RunRequest& request) {
	carve::CarverRegistry registry{matchPathFor(request.forcePortable)};
	carve::registerBuiltinCarvers(registry, viewsOf(request.formats));
	return registry;
}

// Everything a run reports into, so the scan step takes one parameter for them
// rather than three.
struct Recorders {
	fs::EntryVisitor* entries;           // non-owning, never null
	carve::CandidateVisitor* candidates; // non-owning, never null
	recovery::ScanProgress* progress;    // non-owning, never null
};

// One pass over the device with both sources reporting into the visitors. The
// scanner and its registry are locals because they outlive nothing: the run
// ends with this call.
[[nodiscard]] Result<recovery::RecoveryStats> scanInto(
	recovery::RunScope& scope,
	const RunRequest& request,
	const recovery::RecoveryPlan& plan,
	const Recorders& into) {
	const carve::CarverRegistry registry = registryFor(request);
	const carve::SignatureScanner scanner{registry, carve::ScanConfig{}};
	const recovery::HybridRecovery hybrid{scanner, plan};
	return hybrid.run(scope, *into.entries, *into.candidates, *into.progress);
}

[[nodiscard]] recovery::RecoveryPlan
planFor(const RunRequest& request, const OpenSession& session) {
	return recovery::RecoveryPlan{
		.mode = request.mode,
		.resumeFrom = session.resumeFrom,
		.checkpointBytes = recovery::kDefaultCheckpointBytes};
}

[[nodiscard]] Result<recovery::RecoveryStats> indexFindings(
	recovery::RunScope& scope,
	const RunRequest& request,
	OpenSession& session,
	Checkpointer& progress) {
	recovery::IndexingEntryVisitor entries{session.index};
	recovery::IndexingCandidateVisitor candidates{session.index};
	const auto stats = scanInto(
		scope,
		request,
		planFor(request, session),
		Recorders{.entries = &entries, .candidates = &candidates, .progress = &progress});
	return withoutLostRecords(stats, entries.failedAppends() + candidates.failedAppends());
}

// Everything both passes find, appended to the session's index — continuing the
// one already there when it belongs to this run. The index closes as this
// returns, which is what lets the caller read it back.
[[nodiscard]] Result<recovery::RecoveryStats>
scanSession(recovery::RunScope& scope, const RunRequest& request) {
	const std::uint64_t sizeInBytes = scope.device().sizeInBytes();
	auto session = openSession(request, sizeInBytes);
	if (!session.hasValue()) {
		return session.error();
	}
	Checkpointer progress{request.session, shapeOf(request, sizeInBytes), session.value().index};
	return indexFindings(scope, request, session.value(), progress);
}

// The session directory, brought into existence. An existing one is reused; a
// path already taken by something that is not a directory is a typed failure.
[[nodiscard]] Result<std::filesystem::path> prepareSession(const std::filesystem::path& session) {
	std::error_code failure;
	std::filesystem::create_directories(session, failure);
	if (failure) {
		return Error{
			.code = ErrorCode::kIoFailure,
			.offset = 0,
			.osCode = static_cast<std::int32_t>(failure.value())};
	}
	return session;
}

// Discovery, arbitration and extraction, once the scope is resolved and the
// destination has been vouched for.
[[nodiscard]] Result<RunReport> recoverInto(
	const SourceStack& stack,
	recovery::RunScope& scope,
	recovery::RecoverySink& sink,
	const RunRequest& request) {
	const auto session = prepareSession(request.session);
	if (!session.hasValue()) {
		return session.error();
	}
	const auto scanned = scanSession(scope, request);
	if (!scanned.hasValue()) {
		return scanned.error();
	}
	const DeliverySource source{
		.device = scope.device(),
		.stack = stack,
		.startBytes = scope.startBytes()};
	return decideAndDeliver(source, sink, request, scanned.value());
}

// The byte range this run works in. The partition number is all this layer
// decides; what it means is `recovery/`'s answer, from the one reading of the
// table a run gets.
[[nodiscard]] Result<RunReport>
recoverFrom(SourceStack& stack, recovery::RecoverySink& sink, const RunRequest& request) {
	auto scope = recovery::RunScope::resolve(stack.top(), request.partition);
	if (!scope.hasValue()) {
		return scope.error();
	}
	return recoverInto(stack, scope.value(), sink, request);
}

} // namespace

Result<recovery::RecoveryStats>
withoutLostRecords(const Result<recovery::RecoveryStats>& stats, std::uint64_t lostRecords) {
	if (!stats.hasValue() || lostRecords == 0) {
		return stats;
	}
	return Error{.code = ErrorCode::kIoFailure, .offset = 0, .osCode = 0};
}

Result<RunReport> runRecovery(const RunRequest& request) {
	auto source = openSource(request.source);
	if (!source.hasValue()) {
		return source.error();
	}
	auto sink = recovery::RecoverySink::open(request.destination, request.source);
	if (!sink.hasValue()) {
		return sink.error();
	}
	return recoverFrom(source.value(), sink.value(), request);
}

} // namespace revenant::cli

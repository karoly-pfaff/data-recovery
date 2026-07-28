// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/RecoveryRun.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "revenant/carve/BuiltinCarvers.hpp"
#include "revenant/carve/CandidateVisitor.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/core/io/ImageFileDevice.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/recovery/Arbitration.hpp"
#include "revenant/recovery/CandidateIndex.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "revenant/recovery/IndexingVisitors.hpp"
#include "revenant/recovery/RecoverySink.hpp"

namespace revenant::cli {

namespace {

// What discovery produced: the run's own statistics, and the candidates that
// survived arbitration.
struct Discovery {
	recovery::RecoveryStats stats;
	recovery::Arbitration decided;
};

[[nodiscard]] std::vector<std::string_view> viewsOf(const std::vector<std::string>& names) {
	return {names.begin(), names.end()};
}

// The carvers this run searches for. An empty allowlist registers every format
// that ships, so "no filter" is the default rather than "nothing works".
[[nodiscard]] carve::CarverRegistry registryFor(const std::vector<std::string>& formats) {
	carve::CarverRegistry registry;
	carve::registerBuiltinCarvers(registry, viewsOf(formats));
	return registry;
}

// One pass over the device with both sources reporting into the visitors. The
// scanner and its registry are locals because they outlive nothing: the run
// ends with this call.
[[nodiscard]] Result<recovery::RecoveryStats> scanInto(
	BlockDevice& device,
	const RunRequest& request,
	fs::EntryVisitor& entries,
	carve::CandidateVisitor& candidates) {
	const carve::CarverRegistry registry = registryFor(request.formats);
	const carve::SignatureScanner scanner{registry, carve::ScanConfig{}};
	const recovery::HybridRecovery hybrid{scanner, request.mode};
	return hybrid.run(device, entries, candidates);
}

// Everything both passes find, appended to a fresh index in `session`. The
// index closes as this returns, which is what lets the caller read it back.
[[nodiscard]] Result<recovery::RecoveryStats> indexFindings(
	BlockDevice& device,
	const RunRequest& request,
	const std::filesystem::path& session) {
	auto index = recovery::CandidateIndex::create(session);
	if (!index.hasValue()) {
		return index.error();
	}
	recovery::IndexingEntryVisitor entries{index.value()};
	recovery::IndexingCandidateVisitor candidates{index.value()};
	const auto stats = scanInto(device, request, entries, candidates);
	return withoutLostRecords(stats, entries.failedAppends() + candidates.failedAppends());
}

[[nodiscard]] Result<Discovery>
discover(BlockDevice& device, const RunRequest& request, const std::filesystem::path& session) {
	const auto indexed = indexFindings(device, request, session);
	if (!indexed.hasValue()) {
		return indexed.error();
	}
	auto decided = recovery::arbitrateIndex(session);
	if (!decided.hasValue()) {
		return decided.error();
	}
	return Discovery{.stats = indexed.value(), .decided = std::move(decided.value())};
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

[[nodiscard]] RunReport
reportOf(const Discovery& found, const recovery::ExtractionStats& extraction) {
	return RunReport{
		.discovery = found.stats,
		.winners = static_cast<std::uint64_t>(found.decided.winners.size()),
		.suppressed = found.decided.suppressed,
		.extraction = extraction};
}

// Discovery, arbitration and extraction, once the device is open and the
// destination has been vouched for.
[[nodiscard]] Result<RunReport>
recoverInto(BlockDevice& device, recovery::RecoverySink& sink, const RunRequest& request) {
	const auto session = prepareSession(request.session);
	if (!session.hasValue()) {
		return session.error();
	}
	const auto found = discover(device, request, session.value());
	if (!found.hasValue()) {
		return found.error();
	}
	return reportOf(found.value(), sink.extract(found.value().decided.winners, device));
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
	auto device = ImageFileDevice::open(request.source);
	if (!device.hasValue()) {
		return device.error();
	}
	auto sink = recovery::RecoverySink::open(request.destination, request.source);
	if (!sink.hasValue()) {
		return sink.error();
	}
	return recoverInto(*device.value(), sink.value(), request);
}

} // namespace revenant::cli

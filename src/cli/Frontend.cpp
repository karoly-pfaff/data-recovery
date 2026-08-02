// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/Frontend.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cli/Interrupt.hpp"
#include "cli/PartitionListing.hpp"
#include "cli/RecoveryOptions.hpp"
#include "cli/RecoveryRun.hpp"
#include "cli/RunOutcome.hpp"
#include "cli/RunSummary.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/log/LogLevel.hpp"
#include "revenant/core/log/Logger.hpp"
#include "revenant/core/log/StderrSink.hpp"

namespace revenant::cli {

namespace {

constexpr std::string_view kHelpFlag = "--help";

// argv without the program name. Taken as views over argv's own storage, which
// outlives the run.
[[nodiscard]] std::vector<std::string_view> argumentsOf(std::span<char* const> args) {
	if (args.empty()) {
		return {};
	}
	const auto rest = args.subspan(1);
	return std::vector<std::string_view>{rest.begin(), rest.end()};
}

[[nodiscard]] bool wantsHelp(Arguments arguments) {
	return std::ranges::find(arguments, kHelpFlag) != arguments.end();
}

void logLines(const std::vector<std::string>& lines, Logger& logger) {
	for (const std::string& line : lines) {
		logger.log(LogLevel::kInfo, line);
	}
}

// A finished run says what it did; a stopped one says why.
//
// An interrupted scan is `kStoppedResumable` and not a failure: it wrote its
// checkpoint, and the same command carries on from it. That is the one stop
// with no error to classify, which is why it is decided here.
[[nodiscard]] RunOutcome report(const Result<RunReport>& outcome, Logger& logger) {
	if (!outcome.hasValue()) {
		logger.log(LogLevel::kError, describe(outcome.error()));
		return outcomeOf(outcome.error().code);
	}
	logLines(summarize(outcome.value()), logger);
	if (!outcome.value().discovery.scanComplete) {
		return RunOutcome::kStoppedResumable;
	}
	return RunOutcome::kFinished;
}

// A listing that could not open its source says so; one that found nothing says
// that too, and it is an answer rather than a failure.
[[nodiscard]] RunOutcome list(const RunRequest& request, Logger& logger) {
	const auto lines = describePartitions(request.source);
	if (!lines.hasValue()) {
		logger.log(LogLevel::kError, describe(lines.error()));
		return outcomeOf(lines.error().code);
	}
	logLines(lines.value(), logger);
	return RunOutcome::kFinished;
}

[[nodiscard]] RunOutcome perform(const RunRequest& request, Logger& logger) {
	if (request.action == Action::kListPartitions) {
		return list(request, logger);
	}
	return report(runRecovery(request), logger);
}

// An argument list the grammar refuses is answered with the grammar itself:
// the operator needs the shape of the command, not the name of an error code.
[[nodiscard]] RunOutcome
recover(Arguments arguments, std::string_view usage, Grammar grammar, Logger& logger) {
	const auto request = grammar(arguments);
	if (!request.hasValue()) {
		logger.log(LogLevel::kError, usage);
		return RunOutcome::kUsageError;
	}
	return perform(request.value(), logger);
}

} // namespace

RunOutcome runFrontend(std::span<char* const> args, std::string_view usage, Grammar grammar) {
	StderrSink sink;
	Logger logger{sink, LogLevel::kInfo};
	catchInterrupts();
	const auto arguments = argumentsOf(args);
	if (wantsHelp(arguments)) {
		logger.log(LogLevel::kInfo, usage);
		return RunOutcome::kFinished;
	}
	return recover(arguments, usage, grammar, logger);
}

} // namespace revenant::cli

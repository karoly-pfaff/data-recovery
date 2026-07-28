// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/UndeleteCli.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cli/RunSummary.hpp"
#include "cli/UndeleteOptions.hpp"
#include "cli/UndeleteRun.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/log/LogLevel.hpp"
#include "revenant/core/log/Logger.hpp"
#include "revenant/core/log/StderrSink.hpp"

namespace revenant::cli {

namespace {

constexpr std::string_view kUsage =
	"usage: revenant-undelete --source <image> --destination <directory>\n"
	"                        [--hybrid | --fs-only | --carve-only]\n"
	"                        [--session <directory>]";

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

[[nodiscard]] bool wantsHelp(std::span<const std::string_view> arguments) {
	return std::ranges::find(arguments, kHelpFlag) != arguments.end();
}

void logLines(const std::vector<std::string>& lines, Logger& logger) {
	for (const std::string& line : lines) {
		logger.log(LogLevel::kInfo, line);
	}
}

// A finished run says what it did; a stopped one says why.
[[nodiscard]] bool report(const Result<RunReport>& outcome, Logger& logger) {
	if (!outcome.hasValue()) {
		logger.log(LogLevel::kError, describe(outcome.error()));
		return false;
	}
	logLines(summarize(outcome.value()), logger);
	return true;
}

// An argument list the grammar refuses is answered with the grammar itself:
// the operator needs the shape of the command, not the name of an error code.
[[nodiscard]] bool recover(std::span<const std::string_view> arguments, Logger& logger) {
	const auto options = parseUndeleteOptions(arguments);
	if (!options.hasValue()) {
		logger.log(LogLevel::kError, kUsage);
		return false;
	}
	return report(runRecovery(options.value()), logger);
}

} // namespace

bool runUndeleteCli(std::span<char* const> args) {
	StderrSink sink;
	Logger logger{sink, LogLevel::kInfo};
	const auto arguments = argumentsOf(args);
	if (wantsHelp(arguments)) {
		logger.log(LogLevel::kInfo, kUsage);
		return true;
	}
	return recover(arguments, logger);
}

} // namespace revenant::cli

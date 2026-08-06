// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/UndeleteOptions.hpp"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "cli/FlagTable.hpp"
#include "cli/RecoveryOptions.hpp"
#include "cli/RecoveryRun.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/recovery/HybridRecovery.hpp"

namespace revenant::cli {

namespace {

constexpr std::string_view kHybridFlag = "--hybrid";
constexpr std::string_view kFilesystemOnlyFlag = "--fs-only";
constexpr std::string_view kCarveOnlyFlag = "--carve-only";

[[nodiscard]] std::optional<recovery::RecoveryMode> modeOf(std::string_view flag) {
	if (flag == kHybridFlag) {
		return recovery::RecoveryMode::kHybrid;
	}
	if (flag == kFilesystemOnlyFlag) {
		return recovery::RecoveryMode::kFilesystemOnly;
	}
	if (flag == kCarveOnlyFlag) {
		return recovery::RecoveryMode::kCarveOnly;
	}
	return std::nullopt;
}

// The three flags `revenant-undelete` adds. A second mode flag is refused: two
// modes are not a narrowing of one, and picking between them would be a guess.
[[nodiscard]] Result<Arguments> applyModeFlag(OptionDraft& draft, Arguments arguments) {
	const auto mode = modeOf(arguments.front());
	if (!mode.has_value() || draft.mode.has_value()) {
		return usageError();
	}
	draft.mode = mode;
	return arguments.subspan(1);
}

// Shared plus this frontend's own. `--formats` is absent because narrowing the
// carve formats is `revenant-carve`'s question.
[[nodiscard]] const std::vector<FlagDescriptor>& undeleteTable() {
	static const std::vector<FlagDescriptor> kTable = [] {
		std::vector<FlagDescriptor> flags{sharedFlags().begin(), sharedFlags().end()};
		flags.push_back(
			FlagDescriptor{
				.name = kHybridFlag,
				.metavar = "",
				.help = "filesystem metadata first, then carve the rest",
				.read = applyModeFlag});
		flags.push_back(
			FlagDescriptor{
				.name = kFilesystemOnlyFlag,
				.metavar = "",
				.help = "recover only what filesystem metadata names",
				.read = applyModeFlag});
		flags.push_back(
			FlagDescriptor{
				.name = kCarveOnlyFlag,
				.metavar = "",
				.help = "ignore filesystem metadata and carve only",
				.read = applyModeFlag});
		return flags;
	}();
	return kTable;
}

} // namespace

std::span<const FlagDescriptor> undeleteFlags() {
	return undeleteTable();
}

Result<RunRequest> parseUndeleteOptions(Arguments arguments) {
	return readRecoveryOptions(arguments, undeleteFlags(), recovery::RecoveryMode::kHybrid);
}

} // namespace revenant::cli

// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/UndeleteOptions.hpp"

#include <optional>
#include <string_view>

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

// The only flags `revenant-undelete` adds to the shared grammar. Anything else
// lands here too — including a bare path, since the grammar is named-only — and
// is refused.
[[nodiscard]] Result<Arguments> applyModeFlag(OptionDraft& draft, Arguments arguments) {
	const auto mode = modeOf(arguments.front());
	if (!mode.has_value() || draft.mode.has_value()) {
		return usageError();
	}
	draft.mode = mode;
	return arguments.subspan(1);
}

} // namespace

Result<RunRequest> parseUndeleteOptions(Arguments arguments) {
	return readRecoveryOptions(arguments, applyModeFlag, recovery::RecoveryMode::kHybrid);
}

} // namespace revenant::cli

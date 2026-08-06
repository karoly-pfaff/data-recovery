// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/CarveOptions.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cli/FlagTable.hpp"
#include "cli/RecoveryOptions.hpp"
#include "cli/RecoveryRun.hpp"
#include "revenant/carve/BuiltinCarvers.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/recovery/HybridRecovery.hpp"

namespace revenant::cli {

namespace {

constexpr std::string_view kFormatsFlag = "--formats";
constexpr char kFormatSeparator = ',';

// The list as written, separators removed and nothing else interpreted. An
// empty list yields one empty name, which is then refused — a flag that states
// a restriction and names nothing is a mistake, not a request for everything.
[[nodiscard]] std::vector<std::string_view> splitOnSeparators(std::string_view list) {
	std::vector<std::string_view> names;
	for (std::string_view rest = list;;) {
		const auto separator = rest.find(kFormatSeparator);
		names.push_back(rest.substr(0, separator));
		if (separator == std::string_view::npos) {
			return names;
		}
		rest = rest.substr(separator + 1);
	}
}

// Every name in the list, or nothing when one of them names a format no carver
// answers to.
[[nodiscard]] std::optional<std::vector<std::string>> formatsIn(std::string_view list) {
	std::vector<std::string> formats;
	for (const std::string_view name : splitOnSeparators(list)) {
		if (!carve::isBuiltinFormat(name)) {
			return std::nullopt;
		}
		formats.emplace_back(name);
	}
	return formats;
}

[[nodiscard]] Result<Arguments> takeFormats(OptionDraft& draft, const FlagValue& taken) {
	const auto formats = formatsIn(taken.value);
	if (!formats.has_value()) {
		return usageError();
	}
	draft.formats = formats.value();
	return taken.rest;
}

// The only flag `revenant-carve` adds to the shared grammar. A second one is
// refused for the same reason a second mode flag is: two lists are not a
// narrowing of one, and picking between them would be a guess.
[[nodiscard]] Result<Arguments> applyFormatsFlag(OptionDraft& draft, Arguments arguments) {
	if (!draft.formats.empty()) {
		return usageError();
	}
	return valueAfterFlag(arguments).andThen(
		[&draft](const FlagValue& taken) { return takeFormats(draft, taken); });
}

// Shared plus this frontend's own. Composed once; the mode flags are absent
// because `revenant-carve` carves and has no mode to choose.
[[nodiscard]] const std::vector<FlagDescriptor>& carveTable() {
	static const std::vector<FlagDescriptor> table = [] {
		std::vector<FlagDescriptor> flags{sharedFlags().begin(), sharedFlags().end()};
		flags.push_back(
			FlagDescriptor{
				.name = kFormatsFlag,
				.takesValue = true,
				.help = "carve only these formats, comma-separated",
				.read = applyFormatsFlag});
		return flags;
	}();
	return table;
}

} // namespace

std::span<const FlagDescriptor> carveFlags() {
	return carveTable();
}

Result<RunRequest> parseCarveOptions(Arguments arguments) {
	return readRecoveryOptions(arguments, carveFlags(), recovery::RecoveryMode::kCarveOnly);
}

} // namespace revenant::cli

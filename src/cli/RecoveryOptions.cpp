// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/RecoveryOptions.hpp"

#include <filesystem>
#include <string_view>

#include "cli/RecoveryRun.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/recovery/HybridRecovery.hpp"

namespace revenant::cli {

namespace {

constexpr std::string_view kSourceFlag = "--source";
constexpr std::string_view kDestinationFlag = "--destination";
constexpr std::string_view kSessionFlag = "--session";
constexpr std::string_view kDryRunFlag = "--dry-run";

// The path `flag` fills, or nothing when it names no shared path at all.
[[nodiscard]] std::filesystem::path* pathFieldOf(OptionDraft& draft, std::string_view flag) {
	if (flag == kSourceFlag) {
		return &draft.source;
	}
	if (flag == kDestinationFlag) {
		return &draft.destination;
	}
	if (flag == kSessionFlag) {
		return &draft.session;
	}
	return nullptr;
}

// Stopping before extraction is the one thing both frontends do the same way,
// so the flag that asks for it lives here. Stating it twice is refused for the
// same reason a repeated mode flag is.
[[nodiscard]] Result<Arguments> applyDryRun(OptionDraft& draft, Arguments arguments) {
	if (draft.delivery.has_value()) {
		return usageError();
	}
	draft.delivery = Delivery::kPreview;
	return arguments.subspan(1);
}

[[nodiscard]] Result<Arguments> readOne(OptionDraft& draft, Arguments arguments, ExtraFlags extra) {
	if (arguments.front() == kDryRunFlag) {
		return applyDryRun(draft, arguments);
	}
	std::filesystem::path* field = pathFieldOf(draft, arguments.front());
	if (field == nullptr) {
		return extra(draft, arguments);
	}
	return valueAfterFlag(arguments).map([field](const FlagValue& taken) {
		*field = taken.value;
		return taken.rest;
	});
}

[[nodiscard]] Result<OptionDraft> readFlags(Arguments arguments, ExtraFlags extra) {
	OptionDraft draft;
	while (!arguments.empty()) {
		const auto next = readOne(draft, arguments, extra);
		if (!next.hasValue()) {
			return next.error();
		}
		arguments = next.value();
	}
	return draft;
}

// A run with no source has nothing to read, and one with no destination has
// nowhere to put what it finds; neither has a sensible default.
[[nodiscard]] Result<OptionDraft> withRequiredPaths(const OptionDraft& draft) {
	if (draft.source.empty() || draft.destination.empty()) {
		return usageError();
	}
	return draft;
}

[[nodiscard]] std::filesystem::path sessionOf(const OptionDraft& draft) {
	if (!draft.session.empty()) {
		return draft.session;
	}
	return draft.destination / kSessionDirectoryName;
}

[[nodiscard]] RunRequest settled(const OptionDraft& draft, recovery::RecoveryMode defaultMode) {
	return RunRequest{
		.source = draft.source,
		.destination = draft.destination,
		.session = sessionOf(draft),
		.mode = draft.mode.value_or(defaultMode),
		.delivery = draft.delivery.value_or(Delivery::kExtract),
		.formats = draft.formats};
}

} // namespace

Error usageError() {
	return Error{.code = ErrorCode::kInvalidArgument, .offset = 0, .osCode = 0};
}

Result<FlagValue> valueAfterFlag(Arguments arguments) {
	const Arguments afterFlag = arguments.subspan(1);
	if (afterFlag.empty()) {
		return usageError();
	}
	return FlagValue{.value = afterFlag.front(), .rest = afterFlag.subspan(1)};
}

Result<RunRequest>
readRecoveryOptions(Arguments arguments, ExtraFlags extra, recovery::RecoveryMode defaultMode) {
	return readFlags(arguments, extra)
		.andThen(withRequiredPaths)
		.map([defaultMode](const OptionDraft& draft) { return settled(draft, defaultMode); });
}

} // namespace revenant::cli

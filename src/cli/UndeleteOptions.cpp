// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/UndeleteOptions.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/recovery/HybridRecovery.hpp"

namespace revenant::cli {

namespace {

constexpr std::string_view kSourceFlag = "--source";
constexpr std::string_view kDestinationFlag = "--destination";
constexpr std::string_view kSessionFlag = "--session";
constexpr std::string_view kHybridFlag = "--hybrid";
constexpr std::string_view kFilesystemOnlyFlag = "--fs-only";
constexpr std::string_view kCarveOnlyFlag = "--carve-only";

// The arguments a step has not read yet. Flags are consumed from the front, so
// every step hands the next one what is left rather than an index into a shared
// sequence.
using Rest = std::span<const std::string_view>;

// The options as they are being built. The mode is optional only here: it has
// no value until the operator names one, which is how a second one is caught.
struct Draft {
	std::filesystem::path source;
	std::filesystem::path destination;
	std::filesystem::path session;
	std::optional<recovery::RecoveryMode> mode;
};

[[nodiscard]] Error usageError() {
	return Error{.code = ErrorCode::kInvalidArgument, .offset = 0, .osCode = 0};
}

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

// The path `flag` fills, or nothing when it names no path at all.
[[nodiscard]] std::filesystem::path* fieldOf(Draft& draft, std::string_view flag) {
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

// True when the flag was a mode and took effect; an error when it was a second
// mode where one was already chosen.
[[nodiscard]] Result<bool> applyMode(Draft& draft, std::string_view flag) {
	const auto mode = modeOf(flag);
	if (!mode.has_value()) {
		return false;
	}
	if (draft.mode.has_value()) {
		return usageError();
	}
	draft.mode = mode;
	return true;
}

// A flag that takes the next argument as its value. An unknown flag lands here
// too, and is refused — including a bare path, since the grammar is named-only.
[[nodiscard]] Result<Rest> applyValue(Draft& draft, Rest arguments) {
	std::filesystem::path* field = fieldOf(draft, arguments.front());
	const Rest afterFlag = arguments.subspan(1);
	if (field == nullptr || afterFlag.empty()) {
		return usageError();
	}
	*field = afterFlag.front();
	return afterFlag.subspan(1);
}

[[nodiscard]] Result<Rest> applyFlag(Draft& draft, Rest arguments) {
	const auto consumed = applyMode(draft, arguments.front());
	if (!consumed.hasValue()) {
		return consumed.error();
	}
	if (consumed.value()) {
		return arguments.subspan(1);
	}
	return applyValue(draft, arguments);
}

[[nodiscard]] Result<Draft> readFlags(Rest arguments) {
	Draft draft;
	while (!arguments.empty()) {
		const auto next = applyFlag(draft, arguments);
		if (!next.hasValue()) {
			return next.error();
		}
		arguments = next.value();
	}
	return draft;
}

// A run with no source has nothing to read, and one with no destination has
// nowhere to put what it finds; neither has a sensible default.
[[nodiscard]] Result<Draft> withRequiredPaths(const Draft& draft) {
	if (draft.source.empty() || draft.destination.empty()) {
		return usageError();
	}
	return draft;
}

[[nodiscard]] std::filesystem::path sessionOf(const Draft& draft) {
	if (!draft.session.empty()) {
		return draft.session;
	}
	return draft.destination / kSessionDirectoryName;
}

[[nodiscard]] UndeleteOptions settled(const Draft& draft) {
	return UndeleteOptions{
		.source = draft.source,
		.destination = draft.destination,
		.session = sessionOf(draft),
		.mode = draft.mode.value_or(recovery::RecoveryMode::kHybrid)};
}

} // namespace

Result<UndeleteOptions> parseUndeleteOptions(std::span<const std::string_view> arguments) {
	return readFlags(arguments).andThen(withRequiredPaths).map(settled);
}

} // namespace revenant::cli

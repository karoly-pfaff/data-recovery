// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/RecoveryOptions.hpp"

#include <filesystem>
#include <span>

#include "cli/FlagTable.hpp"
#include "cli/RecoveryRun.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "revenant/recovery/RunScope.hpp"

namespace revenant::cli {

namespace {

// One flag, read by whichever descriptor names it. A flag no descriptor names
// is a usage error here rather than at the end of a chain of handlers — which
// is what lets the table be the whole surface. `--help` has no reader: the
// frontend answers it before the grammar runs, so meeting it here means it was
// not consumed, and the grammar has nothing to say about it.
[[nodiscard]] Result<Arguments>
readOne(OptionDraft& draft, Arguments arguments, std::span<const FlagDescriptor> flags) {
	const FlagDescriptor* const flag = flagNamed(flags, arguments.front());
	if (flag == nullptr || flag->read == nullptr) {
		return usageError();
	}
	return flag->read(draft, arguments);
}

[[nodiscard]] Result<OptionDraft>
readFlags(Arguments arguments, std::span<const FlagDescriptor> flags) {
	OptionDraft draft;
	while (!arguments.empty()) {
		const auto next = readOne(draft, arguments, flags);
		if (!next.hasValue()) {
			return next.error();
		}
		arguments = next.value();
	}
	return draft;
}

// A run with no source has nothing to read, and one with no destination has
// nowhere to put what it finds; neither has a sensible default. A *listing*
// writes nothing, so demanding a destination for it would make an operator name
// a place to write before they can find out what is on the disk.
[[nodiscard]] Result<OptionDraft> withRequiredPaths(const OptionDraft& draft) {
	const bool writes = draft.action.value_or(Action::kRecover) == Action::kRecover;
	if (draft.source.empty() || (writes && draft.destination.empty())) {
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
		.action = draft.action.value_or(Action::kRecover),
		.partition = draft.partition.value_or(recovery::kWholeSource),
		.formats = draft.formats,
		.forcePortable = draft.forcePortable.value_or(false)};
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

Result<RunRequest> readRecoveryOptions(
	Arguments arguments,
	std::span<const FlagDescriptor> flags,
	recovery::RecoveryMode defaultMode) {
	return readFlags(arguments, flags)
		.andThen(withRequiredPaths)
		.map([defaultMode](const OptionDraft& draft) { return settled(draft, defaultMode); });
}

} // namespace revenant::cli

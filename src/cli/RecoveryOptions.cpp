// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/RecoveryOptions.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <system_error>

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
constexpr std::string_view kListPartitionsFlag = "--list-partitions";
constexpr std::string_view kPartitionFlag = "--partition";
constexpr std::string_view kForcePortableFlag = "--force-portable";

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

// Asking what is on a disk is not a recovery, so it is an action rather than a
// mode. Stating it twice is refused for the same reason a repeated mode flag is.
[[nodiscard]] Result<Arguments> applyListPartitions(OptionDraft& draft, Arguments arguments) {
	if (draft.action.has_value()) {
		return usageError();
	}
	draft.action = Action::kListPartitions;
	return arguments.subspan(1);
}

// The CPU fast path turned off by hand. Both frontends take it, because both
// scan. Stating it twice is refused for the same reason a repeated mode flag is.
[[nodiscard]] Result<Arguments> applyForcePortable(OptionDraft& draft, Arguments arguments) {
	if (draft.forcePortable.has_value()) {
		return usageError();
	}
	draft.forcePortable = true;
	return arguments.subspan(1);
}

// Partitions are numbered from one, so `0` is not a partition an operator can
// mean — and a whole-disk run is what leaving the flag off already asks for.
// std::from_chars's [first, last) pointer pair is the only overload portable
// across our toolchains; the arithmetic spans one already-bounded string_view.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
[[nodiscard]] Result<std::uint32_t> partitionNumberIn(std::string_view text) {
	std::uint32_t value = 0;
	const auto [end, failure] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (failure != std::errc{} || end != text.data() + text.size() || value == 0) {
		return usageError();
	}
	return value;
}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

[[nodiscard]] Result<Arguments> takePartition(OptionDraft& draft, const FlagValue& taken) {
	return partitionNumberIn(taken.value).map([&draft, &taken](std::uint32_t number) {
		draft.partition = number;
		return taken.rest;
	});
}

[[nodiscard]] Result<Arguments> applyPartition(OptionDraft& draft, Arguments arguments) {
	if (draft.partition.has_value()) {
		return usageError();
	}
	return valueAfterFlag(arguments).andThen(
		[&draft](const FlagValue& taken) { return takePartition(draft, taken); });
}

// The shared flags that fill something other than a path. Null means this
// argument is not one of them.
using FlagReader = Result<Arguments> (*)(OptionDraft&, Arguments);

// A table rather than a chain, because there are now enough of them that the
// chain was the longest function in the file and said nothing a list does not.
struct SharedFlag {
	std::string_view name;
	FlagReader read;
};

constexpr std::array<SharedFlag, 4> kSharedFlags{
	SharedFlag{.name = kDryRunFlag, .read = applyDryRun},
	SharedFlag{.name = kListPartitionsFlag, .read = applyListPartitions},
	SharedFlag{.name = kPartitionFlag, .read = applyPartition},
	SharedFlag{.name = kForcePortableFlag, .read = applyForcePortable}};

[[nodiscard]] FlagReader sharedFlagFor(std::string_view flag) {
	for (const SharedFlag& shared : kSharedFlags) {
		if (shared.name == flag) {
			return shared.read;
		}
	}
	return nullptr;
}

[[nodiscard]] Result<Arguments>
readPathFlag(OptionDraft& draft, Arguments arguments, ExtraFlags extra) {
	std::filesystem::path* field = pathFieldOf(draft, arguments.front());
	if (field == nullptr) {
		return extra(draft, arguments);
	}
	return valueAfterFlag(arguments).map([field](const FlagValue& taken) {
		*field = taken.value;
		return taken.rest;
	});
}

[[nodiscard]] Result<Arguments> readOne(OptionDraft& draft, Arguments arguments, ExtraFlags extra) {
	const FlagReader shared = sharedFlagFor(arguments.front());
	if (shared != nullptr) {
		return shared(draft, arguments);
	}
	return readPathFlag(draft, arguments, extra);
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
		.partition = draft.partition.value_or(0),
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

Result<RunRequest>
readRecoveryOptions(Arguments arguments, ExtraFlags extra, recovery::RecoveryMode defaultMode) {
	return readFlags(arguments, extra)
		.andThen(withRequiredPaths)
		.map([defaultMode](const OptionDraft& draft) { return settled(draft, defaultMode); });
}

} // namespace revenant::cli

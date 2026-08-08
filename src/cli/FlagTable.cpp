// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/FlagTable.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "cli/RecoveryOptions.hpp"
#include "cli/RecoveryRun.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::cli {

namespace {

// A flag that fills a path field. The three shared path flags differ only in
// which field they fill, so they are one reader parameterized by a member
// pointer rather than three copies — and the separate name-to-field mapping
// this replaces is gone with them.
template <std::filesystem::path OptionDraft::* Field>
[[nodiscard]] Result<Arguments> applyPath(OptionDraft& draft, Arguments arguments) {
	return valueAfterFlag(arguments).map([&draft](const FlagValue& taken) {
		draft.*Field = taken.value;
		return taken.rest;
	});
}

// Stopping before extraction is the one thing both frontends do the same way.
// Stating it twice is refused for the same reason a repeated mode flag is.
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

[[nodiscard]] Result<Arguments>
applyAllowUnverifiedDestination(OptionDraft& draft, Arguments arguments) {
	if (draft.allowUnverifiedDestination.has_value()) {
		return usageError();
	}
	draft.allowUnverifiedDestination = true;
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

constexpr std::array<FlagDescriptor, 9> kShared{
	FlagDescriptor{
		.name = kHelpFlag,
		.metavar = "",
		.help = "print this usage and exit",
		.read = nullptr},
	FlagDescriptor{
		.name = "--source",
		.metavar = "<image>",
		.help = "the image file or device to recover from",
		.read = applyPath<&OptionDraft::source>},
	FlagDescriptor{
		.name = "--destination",
		.metavar = "<directory>",
		.help = "the directory recovered files are written to",
		.read = applyPath<&OptionDraft::destination>},
	FlagDescriptor{
		.name = "--session",
		.metavar = "<directory>",
		.help = "where the run's resumable state lives (default: <destination>/.revenant)",
		.read = applyPath<&OptionDraft::session>},
	FlagDescriptor{
		.name = "--dry-run",
		.metavar = "",
		.help = "scan and report, but write no recovered files",
		.read = applyDryRun},
	FlagDescriptor{
		.name = "--list-partitions",
		.metavar = "",
		.help = "list the partitions on the source and exit",
		.read = applyListPartitions},
	FlagDescriptor{
		.name = "--partition",
		.metavar = "<n>",
		.help = "recover only partition <n>, numbered from 1",
		.read = applyPartition},
	FlagDescriptor{
		.name = "--force-portable",
		.metavar = "",
		.help = "disable the CPU-specific scanner fast path",
		.read = applyForcePortable},
	FlagDescriptor{
		.name = "--allow-unverified-destination",
		.metavar = "",
		.help = "proceed when the source's physical identity cannot be resolved, "
				"having checked yourself that the destination is not on it",
		.read = applyAllowUnverifiedDestination}};

} // namespace

std::span<const FlagDescriptor> sharedFlags() {
	return kShared;
}

const FlagDescriptor* flagNamed(std::span<const FlagDescriptor> flags, std::string_view flag) {
	const auto found =
		std::ranges::find(flags, flag, [](const FlagDescriptor& each) { return each.name; });
	return found == flags.end() ? nullptr : &*found;
}

namespace {

// One flag's two lines: what to type, then what it does.
void appendFlag(std::string& text, const FlagDescriptor& flag) {
	text += "  ";
	text += flag.name;
	if (flag.takesValue()) {
		text += ' ';
		text += flag.metavar;
	}
	text += "\n      ";
	text += flag.help;
	text += '\n';
}

} // namespace

std::string renderFlagHelp(std::span<const FlagDescriptor> flags) {
	std::string text = "flags:\n";
	for (const FlagDescriptor& flag : flags) {
		appendFlag(text, flag);
	}
	return text;
}

} // namespace revenant::cli

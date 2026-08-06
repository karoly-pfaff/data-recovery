// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The CLI surface, stated once (story-0702).
//
// A flag is a descriptor: its name, whether it takes a value, the line that
// describes it, and what reading it does. The parser dispatches from this
// table and `--help` renders from it, so the two cannot disagree — the same
// seam `CarveCli::usage()` already uses for format names, so the help can never
// offer a name the allowlist would refuse.
//
// `--help` is the one flag the grammar never sees: `Frontend::wantsHelp` finds
// it anywhere in the arguments and prints the usage before the grammar runs. It
// is declared here because it is part of the surface and must be documented;
// its `read` is null because nothing parses it.

#include <span>
#include <string>
#include <string_view>

#include "cli/RecoveryOptions.hpp"

namespace revenant::cli {

inline constexpr std::string_view kHelpFlag = "--help";

// What one flag is. `read` consumes the flag and its value from the front of
// the arguments; null means the frontend answers this flag before the grammar.
struct FlagDescriptor {
	std::string_view name;
	bool takesValue;
	std::string_view help;
	ExtraFlags read;
};

// The flags every recovery frontend shares, `--help` included. A frontend's own
// table is this plus whatever it adds.
[[nodiscard]] std::span<const FlagDescriptor> sharedFlags();

// The descriptor for `flag`, or null when no descriptor owns it — which is what
// makes an unknown flag a usage error.
[[nodiscard]] const FlagDescriptor*
flagNamed(std::span<const FlagDescriptor> flags, std::string_view flag);

// The flag list as `--help` prints it: one line per flag, name and help column.
// Rendered rather than written, which is the whole point of the table.
[[nodiscard]] std::string renderFlagHelp(std::span<const FlagDescriptor> flags);

// Each frontend's whole surface: shared plus its own. Defined beside the
// frontend's parser, so neither frontend can name the other's flags.
[[nodiscard]] std::span<const FlagDescriptor> carveFlags();
[[nodiscard]] std::span<const FlagDescriptor> undeleteFlags();

} // namespace revenant::cli

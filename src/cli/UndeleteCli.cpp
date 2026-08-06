// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/UndeleteCli.hpp"

#include <span>
#include <string>
#include <string_view>

#include "cli/ExitCodeHelp.hpp"
#include "cli/FlagTable.hpp"
#include "cli/Frontend.hpp"
#include "cli/RunOutcome.hpp"
#include "cli/UndeleteOptions.hpp"

namespace revenant::cli {

namespace {

// The two shapes a command can take, the flags each shape *requires*, and the
// one thing no table encodes — that the three modes are alternatives rather
// than a set. It stops there: the optional flags are the table's answer,
// rendered below, and enumerating them here as well would be the second
// statement of the surface this story exists to remove.
constexpr std::string_view kGrammar =
	"usage: revenant-undelete --source <image> --destination <directory>\n"
	"                        [--hybrid | --fs-only | --carve-only] [flags]\n"
	"       revenant-undelete --source <image> --list-partitions";

} // namespace

// The grammar, the flag list rendered from the table the parser reads
// (story-0702), and what this run's exit status will mean. The last is the half
// that scripts read.
std::string undeleteUsage() {
	std::string text{kGrammar};
	text += '\n';
	text += renderFlagHelp(undeleteFlags());
	text += kExitCodes;
	return text;
}

RunOutcome runUndeleteCli(std::span<char* const> args) {
	return runFrontend(args, undeleteUsage(), parseUndeleteOptions);
}

} // namespace revenant::cli

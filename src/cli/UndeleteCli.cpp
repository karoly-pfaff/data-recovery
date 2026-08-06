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

// How the flags combine — which are required, which alternate. This is prose
// about the shape of the command and no table encodes it, so it stays written.
// *Which* flags exist is the table's answer, below.
constexpr std::string_view kGrammar =
	"usage: revenant-undelete --source <image> --destination <directory>\n"
	"                        [--hybrid | --fs-only | --carve-only]\n"
	"                        [--session <directory>] [--dry-run]\n"
	"                        [--partition <n>] [--force-portable]\n"
	"       revenant-undelete --source <image> --list-partitions";

// The grammar, the flag list rendered from the table the parser reads
// (story-0702), and what this run's exit status will mean. The last is the half
// that scripts read.
[[nodiscard]] std::string usage() {
	std::string text{kGrammar};
	text += '\n';
	text += renderFlagHelp(undeleteFlags());
	text += kExitCodes;
	return text;
}

} // namespace

RunOutcome runUndeleteCli(std::span<char* const> args) {
	return runFrontend(args, usage(), parseUndeleteOptions);
}

} // namespace revenant::cli

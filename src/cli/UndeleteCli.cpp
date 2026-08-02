// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/UndeleteCli.hpp"

#include <span>
#include <string>
#include <string_view>

#include "cli/ExitCodeHelp.hpp"
#include "cli/Frontend.hpp"
#include "cli/UndeleteOptions.hpp"

namespace revenant::cli {

namespace {

constexpr std::string_view kGrammar =
	"usage: revenant-undelete --source <image> --destination <directory>\n"
	"                        [--hybrid | --fs-only | --carve-only]\n"
	"                        [--session <directory>] [--dry-run]\n"
	"                        [--partition <n>] [--force-portable]\n"
	"       revenant-undelete --source <image> --list-partitions";

// The grammar, plus what this run's exit status will mean. Both are what an
// operator reading `--help` needs, and the second is the half that scripts read.
[[nodiscard]] std::string usage() {
	std::string text{kGrammar};
	text += '\n';
	text += kExitCodes;
	return text;
}

} // namespace

RunOutcome runUndeleteCli(std::span<char* const> args) {
	return runFrontend(args, usage(), parseUndeleteOptions);
}

} // namespace revenant::cli

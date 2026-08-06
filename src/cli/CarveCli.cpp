// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/CarveCli.hpp"

#include <span>
#include <string>
#include <string_view>

#include "cli/CarveOptions.hpp"
#include "cli/ExitCodeHelp.hpp"
#include "cli/FlagTable.hpp"
#include "cli/Frontend.hpp"
#include "cli/RunOutcome.hpp"
#include "revenant/carve/BuiltinCarvers.hpp"

namespace revenant::cli {

namespace {

// How the flags combine — which are required, which alternate. This is prose
// about the shape of the command and no table encodes it, so it stays written.
// *Which* flags exist is the table's answer, below.
constexpr std::string_view kGrammar =
	"usage: revenant-carve --source <image> --destination <directory>\n"
	"                     [--formats <ext,ext,...>]\n"
	"                     [--session <directory>] [--dry-run]\n"
	"                     [--partition <n>] [--force-portable]\n"
	"       revenant-carve --source <image> --list-partitions\n"
	"formats:";

// The grammar, the formats this build actually carves, and the flag list. All
// three come from the layer that owns them rather than being restated here, so
// the help can never offer a format the allowlist would refuse or a flag the
// parser would (story-0702).
[[nodiscard]] std::string usage() {
	std::string text{kGrammar};
	for (const std::string_view name : carve::builtinFormatNames()) {
		text += " ";
		text += name;
	}
	text += '\n';
	text += renderFlagHelp(carveFlags());
	text += kExitCodes;
	return text;
}

} // namespace

RunOutcome runCarveCli(std::span<char* const> args) {
	return runFrontend(args, usage(), parseCarveOptions);
}

} // namespace revenant::cli

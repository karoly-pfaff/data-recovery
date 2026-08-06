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

// The two shapes a command can take, and the flags each shape *requires*. It
// deliberately stops there: the optional flags are the table's answer, rendered
// below, and enumerating them here as well would be the second statement of the
// surface this story exists to remove.
constexpr std::string_view kGrammar =
	"usage: revenant-carve --source <image> --destination <directory> [flags]\n"
	"       revenant-carve --source <image> --list-partitions\n"
	"formats:";

} // namespace

// The grammar, the formats this build actually carves, and the flag list. All
// three come from the layer that owns them rather than being restated here, so
// the help can never offer a format the allowlist would refuse or a flag the
// parser would (story-0702).
std::string carveUsage() {
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

RunOutcome runCarveCli(std::span<char* const> args) {
	return runFrontend(args, carveUsage(), parseCarveOptions);
}

} // namespace revenant::cli

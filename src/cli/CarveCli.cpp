// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/CarveCli.hpp"

#include <span>
#include <string>
#include <string_view>

#include "cli/CarveOptions.hpp"
#include "cli/Frontend.hpp"
#include "revenant/carve/BuiltinCarvers.hpp"

namespace revenant::cli {

namespace {

constexpr std::string_view kGrammar =
	"usage: revenant-carve --source <image> --destination <directory>\n"
	"                     [--formats <ext,ext,...>]\n"
	"                     [--session <directory>]\n"
	"formats:";

// The grammar, plus the formats this build actually carves. Listed from the
// carve layer rather than restated here, so the help can never offer a name the
// allowlist would then refuse.
[[nodiscard]] std::string usage() {
	std::string text{kGrammar};
	for (const std::string_view name : carve::builtinFormatNames()) {
		text += " ";
		text += name;
	}
	return text;
}

} // namespace

bool runCarveCli(std::span<char* const> args) {
	return runFrontend(args, usage(), parseCarveOptions);
}

} // namespace revenant::cli

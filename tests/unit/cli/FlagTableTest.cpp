// SPDX-License-Identifier: GPL-3.0-or-later
// The CLI surface is stated once (story-0702): `--help` renders from the table
// the parser reads, so the two cannot disagree.
//
// The correspondence is not plain set equality, because `--help` is not parsed
// by the grammar at all — `Frontend::wantsHelp` consumes it before the grammar
// runs. It is declared universal, rendered in the help, and excluded from what
// the grammar is asked to accept.
#include "cli/FlagTable.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace {

using revenant::cli::carveFlags;
using revenant::cli::FlagDescriptor;
using revenant::cli::kHelpFlag;
using revenant::cli::renderFlagHelp;
using revenant::cli::undeleteFlags;

[[nodiscard]] bool helpNames(std::string_view help, std::string_view flag) {
	return help.find(flag) != std::string_view::npos;
}

// Every flag a frontend's table declares must appear in the help that table
// renders. This is the half that fails when a flag is added to the parser and
// nobody writes its help line.
void expectHelpNamesEveryFlag(std::span<const FlagDescriptor> flags) {
	const std::string help = renderFlagHelp(flags);
	for (const FlagDescriptor& flag : flags) {
		EXPECT_TRUE(helpNames(help, flag.name)) << "help omits " << flag.name;
	}
}

TEST(FlagTable, CarveHelpNamesEveryFlagTheTableDeclares) {
	expectHelpNamesEveryFlag(carveFlags());
}

TEST(FlagTable, UndeleteHelpNamesEveryFlagTheTableDeclares) {
	expectHelpNamesEveryFlag(undeleteFlags());
}

// The other half: no frontend may render a flag it cannot accept. `--formats`
// and the mode flags are the two frontends' own, and neither belongs to both.
TEST(FlagTable, NeitherFrontendRendersTheOthersFlag) {
	const std::string carve = renderFlagHelp(carveFlags());
	const std::string undelete = renderFlagHelp(undeleteFlags());

	EXPECT_FALSE(helpNames(carve, "--hybrid"));
	EXPECT_FALSE(helpNames(carve, "--fs-only"));
	EXPECT_FALSE(helpNames(carve, "--carve-only"));
	EXPECT_FALSE(helpNames(undelete, "--formats"));
}

// `--help` is universal: both tables declare it, because both frontends answer
// it. It is the one flag the grammar never sees.
TEST(FlagTable, BothFrontendsDeclareHelp) {
	const auto declares = [](std::span<const FlagDescriptor> flags) {
		return std::ranges::any_of(flags, [](const FlagDescriptor& flag) {
			return flag.name == kHelpFlag;
		});
	};
	EXPECT_TRUE(declares(carveFlags()));
	EXPECT_TRUE(declares(undeleteFlags()));
}

// A descriptor's `takesValue` is load-bearing — it is what tells the parser to
// consume the next argument — so it is asserted through the table rather than
// trusted, for one flag of each kind.
TEST(FlagTable, ValueTakingIsDeclaredPerFlag) {
	const auto find = [](std::span<const FlagDescriptor> flags, std::string_view name) {
		const auto found =
			std::ranges::find_if(flags, [name](const FlagDescriptor& f) { return f.name == name; });
		EXPECT_NE(found, flags.end()) << "no descriptor for " << name;
		return *found;
	};
	EXPECT_TRUE(find(carveFlags(), "--source").takesValue);
	EXPECT_TRUE(find(carveFlags(), "--formats").takesValue);
	EXPECT_FALSE(find(carveFlags(), "--dry-run").takesValue);
	EXPECT_FALSE(find(undeleteFlags(), "--hybrid").takesValue);
}

// Every flag carries a help line. An empty one renders a blank column and is
// how a flag "documented" by its name alone slips through.
TEST(FlagTable, EveryFlagCarriesANonEmptyHelpLine) {
	for (const auto& flags : {carveFlags(), undeleteFlags()}) {
		for (const FlagDescriptor& flag : flags) {
			EXPECT_FALSE(flag.help.empty()) << flag.name << " has no help line";
		}
	}
}

} // namespace

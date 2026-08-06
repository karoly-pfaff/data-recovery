// SPDX-License-Identifier: GPL-3.0-or-later
// The CLI surface is stated once (story-0702): `--help` renders from the table
// the parser reads, so the two cannot disagree.
//
// These assert against what each frontend's `--help` actually prints, not
// against `renderFlagHelp(table)`. The first version did the latter and was
// tautological: `renderFlagHelp` appends every name it is given, so "every flag
// in the table appears in the help rendered from that table" cannot fail for
// any table, and deleting the rendering from both frontends left the whole
// suite green. The self-audit caught it; deleting the rendering now fails both
// of the first two tests below.
#include "cli/FlagTable.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>

#include "cli/CarveCli.hpp"
#include "cli/UndeleteCli.hpp"

namespace {

using revenant::cli::carveFlags;
using revenant::cli::carveUsage;
using revenant::cli::FlagDescriptor;
using revenant::cli::kHelpFlag;
using revenant::cli::undeleteFlags;
using revenant::cli::undeleteUsage;

[[nodiscard]] bool names(std::string_view text, std::string_view flag) {
	return text.find(flag) != std::string_view::npos;
}

// The load-bearing check: what the binary prints must name every flag its
// parser accepts, with the value spelled the way the synopsis spells it. One
// assertion per helper — GTest's macros expand to several statements each, and
// two of them puts a helper over the size threshold.
void expectUsageNamesFlag(const std::string& usage, const FlagDescriptor& flag) {
	EXPECT_TRUE(names(usage, flag.name)) << "--help does not name " << flag.name;
}

void expectUsageNamesMetavar(const std::string& usage, const FlagDescriptor& flag) {
	EXPECT_TRUE(!flag.takesValue() || names(usage, flag.metavar))
		<< flag.name << " renders without its metavariable";
}

void expectUsageNames(const std::string& usage, const FlagDescriptor& flag) {
	expectUsageNamesFlag(usage, flag);
	expectUsageNamesMetavar(usage, flag);
}

TEST(FlagTable, CarveUsageNamesEveryFlagTheParserAccepts) {
	const std::string usage = carveUsage();
	for (const FlagDescriptor& flag : carveFlags()) {
		expectUsageNames(usage, flag);
	}
}

TEST(FlagTable, UndeleteUsageNamesEveryFlagTheParserAccepts) {
	const std::string usage = undeleteUsage();
	for (const FlagDescriptor& flag : undeleteFlags()) {
		expectUsageNames(usage, flag);
	}
}

// Neither binary may offer a flag it would then refuse.
TEST(FlagTable, NeitherUsageNamesTheOtherFrontendsFlag) {
	EXPECT_FALSE(names(carveUsage(), "--hybrid"));
	EXPECT_FALSE(names(carveUsage(), "--fs-only"));
	EXPECT_FALSE(names(carveUsage(), "--carve-only"));
	EXPECT_FALSE(names(undeleteUsage(), "--formats"));
}

// `--help` is universal, and it is the one flag the grammar never parses —
// `Frontend::wantsHelp` consumes it first — which is why it carries no reader.
[[nodiscard]] const FlagDescriptor* helpIn(std::span<const FlagDescriptor> flags) {
	const auto found = std::ranges::find(flags, kHelpFlag, &FlagDescriptor::name);
	return found == flags.end() ? nullptr : &*found;
}

TEST(FlagTable, CarveDeclaresHelpAndNothingParsesIt) {
	const FlagDescriptor* const help = helpIn(carveFlags());
	ASSERT_NE(help, nullptr);
	EXPECT_EQ(help->read, nullptr);
}

TEST(FlagTable, UndeleteDeclaresHelpAndNothingParsesIt) {
	const FlagDescriptor* const help = helpIn(undeleteFlags());
	ASSERT_NE(help, nullptr);
	EXPECT_EQ(help->read, nullptr);
}

// A flag with no help line renders a blank description — the case the story
// specified, "fails when a flag is added to the parser without a help line".
// `metavar` is empty exactly when the flag takes no value, so `takesValue()`
// and the reader cannot disagree about whether an argument follows.
void expectHasHelpLine(const FlagDescriptor& flag) {
	EXPECT_FALSE(flag.help.empty()) << flag.name << " has no help line";
}

void expectMetavarMatchesValueTaking(const FlagDescriptor& flag) {
	EXPECT_EQ(flag.takesValue(), !flag.metavar.empty()) << flag.name;
}

void expectWellFormed(const FlagDescriptor& flag) {
	expectHasHelpLine(flag);
	expectMetavarMatchesValueTaking(flag);
}

TEST(FlagTable, EveryCarveFlagIsWellFormed) {
	for (const FlagDescriptor& flag : carveFlags()) {
		expectWellFormed(flag);
	}
}

TEST(FlagTable, EveryUndeleteFlagIsWellFormed) {
	for (const FlagDescriptor& flag : undeleteFlags()) {
		expectWellFormed(flag);
	}
}

} // namespace

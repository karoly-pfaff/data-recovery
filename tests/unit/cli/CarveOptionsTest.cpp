// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/CarveOptions.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "cli/RecoveryOptions.hpp"
#include "cli/RecoveryRun.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/recovery/HybridRecovery.hpp"

namespace {

using revenant::ErrorCode;
using revenant::cli::Delivery;
using revenant::cli::kSessionDirectoryName;
using revenant::cli::parseCarveOptions;
using revenant::cli::RunRequest;
using revenant::recovery::RecoveryMode;

using CommandLine = std::vector<std::string_view>;

[[nodiscard]] CommandLine required() {
	return {"--source", "disk.img", "--destination", "out"};
}

[[nodiscard]] CommandLine requiredPlus(std::string_view flag, std::string_view value) {
	CommandLine arguments = required();
	arguments.push_back(flag);
	arguments.push_back(value);
	return arguments;
}

[[nodiscard]] RunRequest parsed(const CommandLine& arguments) {
	const auto request = parseCarveOptions(arguments);
	EXPECT_TRUE(request.hasValue());
	return request.value();
}

[[nodiscard]] ErrorCode refusalOf(const CommandLine& arguments) {
	const auto request = parseCarveOptions(arguments);
	EXPECT_FALSE(request.hasValue());
	return request.error().code;
}

TEST(CarveOptions, TakesTheSourceAndDestinationItWasGiven) {
	const auto request = parsed(required());
	EXPECT_EQ(request.source, std::filesystem::path{"disk.img"});
	EXPECT_EQ(request.destination, std::filesystem::path{"out"});
}

// There is nothing to choose: carving is what this binary does.
TEST(CarveOptions, AlwaysCarvesAndOnlyCarves) {
	EXPECT_EQ(parsed(required()).mode, RecoveryMode::kCarveOnly);
}

TEST(CarveOptions, RefusesAModeFlagBecauseItHasOnlyOneMode) {
	CommandLine arguments = required();
	arguments.emplace_back("--fs-only");
	EXPECT_EQ(refusalOf(arguments), ErrorCode::kInvalidArgument);
}

// An empty allowlist is what `registerBuiltinCarvers` already calls "carve
// everything", so saying nothing about formats searches for all of them.
TEST(CarveOptions, SearchesForEveryFormatWhenNoneAreNamed) {
	EXPECT_TRUE(parsed(required()).formats.empty());
}

TEST(CarveOptions, TakesTheFormatsItWasNamed) {
	const std::vector<std::string> expected{"jpg", "png"};
	EXPECT_EQ(parsed(requiredPlus("--formats", "jpg,png")).formats, expected);
}

// `tiff` looks right and is wrong — the RAW carver reports `tif`. Obeying it
// would produce a scan that searched for nothing and reported success.
TEST(CarveOptions, RefusesAFormatNoCarverAnswersTo) {
	EXPECT_EQ(refusalOf(requiredPlus("--formats", "tiff")), ErrorCode::kInvalidArgument);
}

TEST(CarveOptions, RefusesAFormatListThatNamesNothing) {
	EXPECT_EQ(refusalOf(requiredPlus("--formats", "")), ErrorCode::kInvalidArgument);
}

TEST(CarveOptions, RefusesAFormatListWithAnEmptyEntry) {
	EXPECT_EQ(refusalOf(requiredPlus("--formats", "jpg,,png")), ErrorCode::kInvalidArgument);
}

TEST(CarveOptions, RefusesASecondFormatList) {
	CommandLine arguments = requiredPlus("--formats", "jpg");
	arguments.emplace_back("--formats");
	arguments.emplace_back("png");
	EXPECT_EQ(refusalOf(arguments), ErrorCode::kInvalidArgument);
}

TEST(CarveOptions, RefusesAFormatFlagWithNothingAfterIt) {
	CommandLine arguments = required();
	arguments.emplace_back("--formats");
	EXPECT_EQ(refusalOf(arguments), ErrorCode::kInvalidArgument);
}

// The shared flag, so carving previews exactly the way undeleting does.
TEST(CarveOptions, StopsBeforeExtractionWhenToldTo) {
	CommandLine arguments = required();
	arguments.emplace_back("--dry-run");
	EXPECT_EQ(parsed(required()).delivery, Delivery::kExtract);
	EXPECT_EQ(parsed(arguments).delivery, Delivery::kPreview);
}

TEST(CarveOptions, RefusesACommandLineWithNoSource) {
	EXPECT_EQ(refusalOf({"--destination", "out"}), ErrorCode::kInvalidArgument);
}

TEST(CarveOptions, RefusesAFlagItDoesNotKnow) {
	CommandLine arguments = required();
	arguments.emplace_back("--deep");
	EXPECT_EQ(refusalOf(arguments), ErrorCode::kInvalidArgument);
}

// `--help` is in the flag table with a null reader — the frontend answers it
// before the grammar runs (story-0702). Reaching the grammar with it therefore
// means it was not consumed, and the only thing between that and calling a null
// function pointer is one guard in `readOne`. This is the test that fails if the
// guard is dropped; the binaries never take this path.
TEST(CarveOptions, RefusesHelpBecauseTheFrontendShouldHaveConsumedIt) {
	CommandLine arguments = required();
	arguments.emplace_back("--help");
	EXPECT_EQ(refusalOf(arguments), ErrorCode::kInvalidArgument);
}

// The session rules are the shared ones, so a carve run states them the same
// way an undelete run does.
TEST(CarveOptions, PutsTheSessionUnderTheDestinationByDefault) {
	EXPECT_EQ(parsed(required()).session, std::filesystem::path{"out"} / kSessionDirectoryName);
}

TEST(CarveOptions, TakesAnExplicitSessionDirectoryInstead) {
	EXPECT_EQ(
		parsed(requiredPlus("--session", "elsewhere")).session,
		std::filesystem::path{"elsewhere"});
}

// The partition rules are the shared ones too, so carving one partition is
// stated the same way as undeleting from it (story-0405).
TEST(CarveOptions, TakesThePartitionNumberItWasGiven) {
	EXPECT_EQ(parsed(requiredPlus("--partition", "3")).partition, 3U);
}

TEST(CarveOptions, RefusesAPartitionThatIsNotANumber) {
	EXPECT_EQ(refusalOf(requiredPlus("--partition", "last")), ErrorCode::kInvalidArgument);
}

TEST(CarveOptions, ListsPartitionsWithoutADestination) {
	CommandLine arguments{"--source", "disk.img"};
	arguments.emplace_back("--list-partitions");
	EXPECT_EQ(parsed(arguments).action, revenant::cli::Action::kListPartitions);
}

// story-0707. The flag affirms rather than disables: it says the operator
// checked what the tool could not, and it is long and awkward on purpose —
// typed by someone who decided, not by someone clearing an obstacle.
TEST(CarveOptions, AnUnverifiedDestinationIsNotAllowedUnlessAskedFor) {
	EXPECT_FALSE(parsed(required()).allowUnverifiedDestination);
}

TEST(CarveOptions, AnUnverifiedDestinationIsAllowedWhenAskedFor) {
	CommandLine arguments = required();
	arguments.push_back("--allow-unverified-destination");
	EXPECT_TRUE(parsed(arguments).allowUnverifiedDestination);
}

// Every other boolean flag refuses a repeat; one that quietly accepted it would
// be the odd entry in a table whose point is that the surface is stated once.
TEST(CarveOptions, AnUnverifiedDestinationIsRefusedWhenStatedTwice) {
	CommandLine arguments = required();
	arguments.push_back("--allow-unverified-destination");
	arguments.push_back("--allow-unverified-destination");
	EXPECT_EQ(refusalOf(arguments), ErrorCode::kInvalidArgument);
}

} // namespace

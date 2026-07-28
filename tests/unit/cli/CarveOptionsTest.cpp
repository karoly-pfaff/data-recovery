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

TEST(CarveOptions, RefusesACommandLineWithNoSource) {
	EXPECT_EQ(refusalOf({"--destination", "out"}), ErrorCode::kInvalidArgument);
}

TEST(CarveOptions, RefusesAFlagItDoesNotKnow) {
	CommandLine arguments = required();
	arguments.emplace_back("--deep");
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

} // namespace

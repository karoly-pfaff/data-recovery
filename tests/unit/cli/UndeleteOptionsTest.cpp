// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/UndeleteOptions.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string_view>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/recovery/HybridRecovery.hpp"

namespace {

using revenant::ErrorCode;
using revenant::cli::parseUndeleteOptions;
using revenant::cli::UndeleteOptions;
using revenant::recovery::RecoveryMode;

using Arguments = std::vector<std::string_view>;

// The two flags every run needs, so each test states only what it is about.
[[nodiscard]] Arguments required() {
	return {"--source", "disk.img", "--destination", "out"};
}

[[nodiscard]] Arguments requiredPlus(std::string_view flag) {
	Arguments arguments = required();
	arguments.push_back(flag);
	return arguments;
}

[[nodiscard]] UndeleteOptions parsed(const Arguments& arguments) {
	const auto options = parseUndeleteOptions(arguments);
	EXPECT_TRUE(options.hasValue());
	return options.value();
}

[[nodiscard]] ErrorCode refusalOf(const Arguments& arguments) {
	const auto options = parseUndeleteOptions(arguments);
	EXPECT_FALSE(options.hasValue());
	return options.error().code;
}

TEST(UndeleteOptions, TakesTheSourceAndDestinationItWasGiven) {
	const auto options = parsed(required());
	EXPECT_EQ(options.source, std::filesystem::path{"disk.img"});
	EXPECT_EQ(options.destination, std::filesystem::path{"out"});
}

TEST(UndeleteOptions, RecoversInHybridModeWhenNoModeIsNamed) {
	EXPECT_EQ(parsed(required()).mode, RecoveryMode::kHybrid);
}

TEST(UndeleteOptions, SelectsTheFilesystemOnlyMode) {
	EXPECT_EQ(parsed(requiredPlus("--fs-only")).mode, RecoveryMode::kFilesystemOnly);
}

TEST(UndeleteOptions, SelectsTheCarveOnlyMode) {
	EXPECT_EQ(parsed(requiredPlus("--carve-only")).mode, RecoveryMode::kCarveOnly);
}

TEST(UndeleteOptions, SelectsHybridModeExplicitly) {
	EXPECT_EQ(parsed(requiredPlus("--hybrid")).mode, RecoveryMode::kHybrid);
}

// Two contradictory instructions are not a refinement of one, and guessing
// which was meant is the silent-wrong-thing the contract forbids.
TEST(UndeleteOptions, RefusesTwoModesThatContradictEachOther) {
	Arguments arguments = requiredPlus("--fs-only");
	arguments.emplace_back("--carve-only");
	EXPECT_EQ(refusalOf(arguments), ErrorCode::kInvalidArgument);
}

TEST(UndeleteOptions, RefusesTheSameModeStatedTwice) {
	Arguments arguments = requiredPlus("--hybrid");
	arguments.emplace_back("--hybrid");
	EXPECT_EQ(refusalOf(arguments), ErrorCode::kInvalidArgument);
}

TEST(UndeleteOptions, RefusesACommandLineWithNoSource) {
	EXPECT_EQ(refusalOf({"--destination", "out"}), ErrorCode::kInvalidArgument);
}

TEST(UndeleteOptions, RefusesACommandLineWithNoDestination) {
	EXPECT_EQ(refusalOf({"--source", "disk.img"}), ErrorCode::kInvalidArgument);
}

TEST(UndeleteOptions, RefusesAnEmptyCommandLine) {
	EXPECT_EQ(refusalOf({}), ErrorCode::kInvalidArgument);
}

TEST(UndeleteOptions, RefusesAFlagItDoesNotKnow) {
	EXPECT_EQ(refusalOf(requiredPlus("--deep")), ErrorCode::kInvalidArgument);
}

// A bare path is not a flag; the grammar is named-only so nothing is recovered
// to or from a place the operator did not spell out.
TEST(UndeleteOptions, RefusesAPositionalArgument) {
	EXPECT_EQ(refusalOf(requiredPlus("disk.img")), ErrorCode::kInvalidArgument);
}

TEST(UndeleteOptions, RefusesAValueFlagWithNothingAfterIt) {
	EXPECT_EQ(refusalOf(requiredPlus("--session")), ErrorCode::kInvalidArgument);
}

TEST(UndeleteOptions, PutsTheSessionUnderTheDestinationByDefault) {
	EXPECT_EQ(
		parsed(required()).session,
		std::filesystem::path{"out"} / revenant::cli::kSessionDirectoryName);
}

TEST(UndeleteOptions, TakesAnExplicitSessionDirectoryInstead) {
	Arguments arguments = requiredPlus("--session");
	arguments.emplace_back("elsewhere");
	EXPECT_EQ(parsed(arguments).session, std::filesystem::path{"elsewhere"});
}

} // namespace

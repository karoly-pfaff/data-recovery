// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/UndeleteOptions.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string_view>
#include <vector>

#include "cli/RecoveryOptions.hpp"
#include "cli/RecoveryRun.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/recovery/HybridRecovery.hpp"

namespace {

using revenant::ErrorCode;
using revenant::cli::kSessionDirectoryName;
using revenant::cli::parseUndeleteOptions;
using revenant::cli::RunRequest;
using revenant::recovery::RecoveryMode;

using CommandLine = std::vector<std::string_view>;

// The two flags every run needs, so each test states only what it is about.
[[nodiscard]] CommandLine required() {
	return {"--source", "disk.img", "--destination", "out"};
}

[[nodiscard]] CommandLine requiredPlus(std::string_view flag) {
	CommandLine arguments = required();
	arguments.push_back(flag);
	return arguments;
}

[[nodiscard]] RunRequest parsed(const CommandLine& arguments) {
	const auto request = parseUndeleteOptions(arguments);
	EXPECT_TRUE(request.hasValue());
	return request.value();
}

[[nodiscard]] ErrorCode refusalOf(const CommandLine& arguments) {
	const auto request = parseUndeleteOptions(arguments);
	EXPECT_FALSE(request.hasValue());
	return request.error().code;
}

TEST(UndeleteOptions, TakesTheSourceAndDestinationItWasGiven) {
	const auto request = parsed(required());
	EXPECT_EQ(request.source, std::filesystem::path{"disk.img"});
	EXPECT_EQ(request.destination, std::filesystem::path{"out"});
}

TEST(UndeleteOptions, RecoversInHybridModeWhenNoModeIsNamed) {
	EXPECT_EQ(parsed(required()).mode, RecoveryMode::kHybrid);
}

// The allowlist belongs to `revenant-carve`; an undelete run carves every
// format over whatever its filesystem pass did not account for.
TEST(UndeleteOptions, SearchesForEveryFormat) {
	EXPECT_TRUE(parsed(required()).formats.empty());
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
	CommandLine arguments = requiredPlus("--fs-only");
	arguments.emplace_back("--carve-only");
	EXPECT_EQ(refusalOf(arguments), ErrorCode::kInvalidArgument);
}

TEST(UndeleteOptions, RefusesTheSameModeStatedTwice) {
	CommandLine arguments = requiredPlus("--hybrid");
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
	EXPECT_EQ(parsed(required()).session, std::filesystem::path{"out"} / kSessionDirectoryName);
}

TEST(UndeleteOptions, TakesAnExplicitSessionDirectoryInstead) {
	CommandLine arguments = requiredPlus("--session");
	arguments.emplace_back("elsewhere");
	EXPECT_EQ(parsed(arguments).session, std::filesystem::path{"elsewhere"});
}

} // namespace

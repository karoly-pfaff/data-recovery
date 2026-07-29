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
using revenant::cli::Delivery;
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

// Stopping before extraction is the same run, one step shorter (ADR-0006).
TEST(UndeleteOptions, ExtractsUnlessToldToStopBeforeIt) {
	EXPECT_EQ(parsed(required()).delivery, Delivery::kExtract);
	EXPECT_EQ(parsed(requiredPlus("--dry-run")).delivery, Delivery::kPreview);
}

TEST(UndeleteOptions, RefusesASecondDryRunFlag) {
	CommandLine arguments = requiredPlus("--dry-run");
	arguments.emplace_back("--dry-run");
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

// --- Partitions (story-0045) -------------------------------------------------

// Zero is the whole source, and it is what leaving the flag off asks for.
TEST(UndeleteOptions, WorksOverTheWholeSourceWhenNoPartitionIsNamed) {
	EXPECT_EQ(parsed(required()).partition, 0U);
	EXPECT_EQ(parsed(required()).action, revenant::cli::Action::kRecover);
}

TEST(UndeleteOptions, TakesThePartitionNumberItWasGiven) {
	CommandLine arguments = requiredPlus("--partition");
	arguments.emplace_back("2");
	EXPECT_EQ(parsed(arguments).partition, 2U);
}

// Partitions are numbered from one, so zero is not a partition anyone can mean.
TEST(UndeleteOptions, RefusesPartitionZero) {
	CommandLine arguments = requiredPlus("--partition");
	arguments.emplace_back("0");
	EXPECT_EQ(refusalOf(arguments), ErrorCode::kInvalidArgument);
}

TEST(UndeleteOptions, RefusesAPartitionThatIsNotANumber) {
	CommandLine arguments = requiredPlus("--partition");
	arguments.emplace_back("first");
	EXPECT_EQ(refusalOf(arguments), ErrorCode::kInvalidArgument);
}

TEST(UndeleteOptions, RefusesTwoPartitionFlags) {
	CommandLine arguments = requiredPlus("--partition");
	arguments.emplace_back("1");
	arguments.emplace_back("--partition");
	arguments.emplace_back("2");
	EXPECT_EQ(refusalOf(arguments), ErrorCode::kInvalidArgument);
}

// Listing writes nothing, so demanding somewhere to write would make an
// operator name a destination before they can find out what is on the disk.
TEST(UndeleteOptions, ListsPartitionsWithoutADestination) {
	const auto request = parsed({"--source", "disk.img", "--list-partitions"});
	EXPECT_EQ(request.action, revenant::cli::Action::kListPartitions);
	EXPECT_EQ(request.source, std::filesystem::path{"disk.img"});
}

TEST(UndeleteOptions, StillNeedsASourceToListPartitionsOf) {
	EXPECT_EQ(refusalOf({"--list-partitions"}), ErrorCode::kInvalidArgument);
}

TEST(UndeleteOptions, RefusesTwoListPartitionsFlags) {
	CommandLine arguments = requiredPlus("--list-partitions");
	arguments.emplace_back("--list-partitions");
	EXPECT_EQ(refusalOf(arguments), ErrorCode::kInvalidArgument);
}

} // namespace

// SPDX-License-Identifier: GPL-3.0-or-later
// The first real binary, driven the way an operator drives it: an argument
// vector in, recovered files on disk out. What is under test here is the
// wiring — that each mode reaches the engine, and that the paths the operator
// named are the paths that get used.
#include "cli/UndeleteCli.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "cli/UndeleteOptions.hpp"
#include "imagegen/ntfs/NtfsImageBuilder.hpp"
#include "support/FixtureContent.hpp"
#include "support/TempDir.hpp"
#include "support/TempFile.hpp"

namespace {

using revenant::cli::kSessionDirectoryName;
using revenant::cli::runUndeleteCli;
using revenant::imagegen::ntfs::buildNtfsImage;
using revenant::testing::fixtureContentNamed;
using revenant::testing::readFileBytes;
using revenant::testing::TempDir;
using revenant::testing::TempFile;

[[nodiscard]] std::vector<char*> pointersTo(std::vector<std::string>& arguments) {
	std::vector<char*> argv;
	argv.reserve(arguments.size());
	for (std::string& argument : arguments) {
		argv.push_back(argument.data());
	}
	return argv;
}

[[nodiscard]] bool runWith(std::vector<std::string> arguments) {
	const auto argv = pointersTo(arguments);
	return runUndeleteCli(std::span<char* const>{argv});
}

[[nodiscard]] bool holdsAnyJpeg(const std::filesystem::path& directory) {
	const std::filesystem::recursive_directory_iterator entries{directory};
	return std::ranges::any_of(entries, [](const std::filesystem::directory_entry& entry) {
		return entry.path().extension() == ".jpg";
	});
}

// One fixture image and one destination, driven by whatever flags a test adds.
class UndeleteCli : public ::testing::Test {
protected:
	UndeleteCli() : image_(buildNtfsImage()) {}

	[[nodiscard]] bool recover(const std::vector<std::string>& flags) {
		return runWith(commandLine(flags));
	}

	[[nodiscard]] std::filesystem::path recovered(const std::string& relative) const {
		return output_.path() / std::filesystem::path{relative};
	}

	[[nodiscard]] const std::filesystem::path& source() const noexcept {
		return image_.path();
	}

	[[nodiscard]] const std::filesystem::path& destination() const noexcept {
		return output_.path();
	}

private:
	[[nodiscard]] std::vector<std::string>
	commandLine(const std::vector<std::string>& flags) const {
		std::vector<std::string> arguments{
			"revenant-undelete",
			"--source",
			source().string(),
			"--destination",
			destination().string()};
		arguments.insert(arguments.end(), flags.begin(), flags.end());
		return arguments;
	}

	TempFile image_;
	TempDir output_;
};

// The default: names where the metadata survived, carving where it did not.
TEST_F(UndeleteCli, HybridRecoveryBringsBackNamedAndCarvedFilesTogether) {
	ASSERT_TRUE(recover({}));
	EXPECT_EQ(readFileBytes(recovered("photos/deleted.jpg")), fixtureContentNamed("deleted.jpg"));
	EXPECT_TRUE(holdsAnyJpeg(recovered("carved")));
}

TEST_F(UndeleteCli, FilesystemOnlyKeepsTheNamesAndCarvesNothing) {
	ASSERT_TRUE(recover({"--fs-only"}));
	EXPECT_EQ(readFileBytes(recovered("photos/deleted.jpg")), fixtureContentNamed("deleted.jpg"));
	EXPECT_FALSE(std::filesystem::exists(recovered("carved")));
}

// No filesystem is consulted, so nothing has a name to keep — which is exactly
// the mode a formatted volume needs.
TEST_F(UndeleteCli, CarveOnlyReconstructsNoNames) {
	ASSERT_TRUE(recover({"--carve-only"}));
	EXPECT_FALSE(std::filesystem::exists(recovered("photos")));
	EXPECT_TRUE(holdsAnyJpeg(recovered("carved")));
}

TEST_F(UndeleteCli, PutsTheRunsIndexUnderTheDestinationByDefault) {
	ASSERT_TRUE(recover({}));
	EXPECT_TRUE(
		std::filesystem::is_directory(
			destination() / std::filesystem::path{kSessionDirectoryName}));
}

TEST_F(UndeleteCli, PutsTheRunsIndexWhereverItWasTold) {
	const TempDir elsewhere;
	ASSERT_TRUE(recover({"--session", (elsewhere.path() / "run").string()}));
	EXPECT_TRUE(std::filesystem::is_directory(elsewhere.path() / "run"));
	EXPECT_FALSE(
		std::filesystem::exists(destination() / std::filesystem::path{kSessionDirectoryName}));
}

TEST_F(UndeleteCli, RefusesTwoModesThatContradictEachOther) {
	EXPECT_FALSE(recover({"--fs-only", "--carve-only"}));
}

TEST_F(UndeleteCli, RefusesAFlagItDoesNotKnow) {
	EXPECT_FALSE(recover({"--deep"}));
}

// The destination is checked before the scan, so a doomed run stops at once.
TEST_F(UndeleteCli, RefusesADestinationThatIsAFileRatherThanADirectory) {
	EXPECT_FALSE(runWith(
		{"revenant-undelete", "--source", source().string(), "--destination", source().string()}));
}

TEST_F(UndeleteCli, RefusesASourceThatIsNotThere) {
	EXPECT_FALSE(runWith(
		{"revenant-undelete",
		 "--source",
		 (destination() / "absent.img").string(),
		 "--destination",
		 destination().string()}));
}

TEST(UndeleteCliArguments, RefusesACommandLineWithNoArguments) {
	EXPECT_FALSE(runWith({"revenant-undelete"}));
}

TEST(UndeleteCliArguments, AnswersHelpWithTheUsageAndSucceeds) {
	EXPECT_TRUE(runWith({"revenant-undelete", "--help"}));
}

} // namespace

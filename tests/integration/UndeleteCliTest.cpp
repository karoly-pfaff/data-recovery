// SPDX-License-Identifier: GPL-3.0-or-later
// The undelete binary, driven the way an operator drives it: an argument vector
// in, recovered files on disk out. What is under test here is the wiring — that
// each mode reaches the engine, and that the paths the operator named are the
// paths that get used.
#include "cli/UndeleteCli.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "cli/RecoveryOptions.hpp"
#include "revenant/core/Sha256.hpp"
#include "revenant/recovery/Manifest.hpp"
#include "support/CliFixture.hpp"
#include "support/FixtureContent.hpp"
#include "support/TempDir.hpp"

namespace {

using revenant::cli::kSessionDirectoryName;
using revenant::cli::runUndeleteCli;
using revenant::recovery::kManifestFileName;
using revenant::testing::CliFixture;
using revenant::testing::fixtureContentNamed;
using revenant::testing::holdsFileOfType;
using revenant::testing::readFileBytes;
using revenant::testing::readFileText;
using revenant::testing::runCli;
using revenant::testing::TempDir;

class UndeleteCli : public ::testing::Test {
protected:
	[[nodiscard]] bool recover(const std::vector<std::string>& flags) const {
		return fixture_.run(runUndeleteCli, flags);
	}

	[[nodiscard]] const CliFixture& fixture() const noexcept {
		return fixture_;
	}

private:
	CliFixture fixture_;
};

// The default: names where the metadata survived, carving where it did not.
TEST_F(UndeleteCli, HybridRecoveryBringsBackNamedAndCarvedFilesTogether) {
	ASSERT_TRUE(recover({}));
	EXPECT_EQ(
		readFileBytes(fixture().recovered("photos/deleted.jpg")),
		fixtureContentNamed("deleted.jpg"));
	EXPECT_TRUE(holdsFileOfType(fixture().recovered("carved"), ".jpg"));
}

TEST_F(UndeleteCli, FilesystemOnlyKeepsTheNamesAndCarvesNothing) {
	ASSERT_TRUE(recover({"--fs-only"}));
	EXPECT_EQ(
		readFileBytes(fixture().recovered("photos/deleted.jpg")),
		fixtureContentNamed("deleted.jpg"));
	EXPECT_FALSE(std::filesystem::exists(fixture().recovered("carved")));
}

// No filesystem is consulted, so nothing has a name to keep — which is exactly
// the mode a formatted volume needs.
TEST_F(UndeleteCli, CarveOnlyReconstructsNoNames) {
	ASSERT_TRUE(recover({"--carve-only"}));
	EXPECT_FALSE(std::filesystem::exists(fixture().recovered("photos")));
	EXPECT_TRUE(holdsFileOfType(fixture().recovered("carved"), ".jpg"));
}

TEST_F(UndeleteCli, PutsTheRunsIndexUnderTheDestinationByDefault) {
	ASSERT_TRUE(recover({}));
	EXPECT_TRUE(
		std::filesystem::is_directory(
			fixture().destination() / std::filesystem::path{kSessionDirectoryName}));
}

TEST_F(UndeleteCli, PutsTheRunsIndexWhereverItWasTold) {
	const TempDir elsewhere;
	ASSERT_TRUE(recover({"--session", (elsewhere.path() / "run").string()}));
	EXPECT_TRUE(std::filesystem::is_directory(elsewhere.path() / "run"));
	EXPECT_FALSE(
		std::filesystem::exists(
			fixture().destination() / std::filesystem::path{kSessionDirectoryName}));
}

// The run leaves a record of itself that someone who did not watch it happen
// can check the recovered bytes against.
TEST_F(UndeleteCli, LeavesAManifestThatVouchesForWhatItRecovered) {
	ASSERT_TRUE(recover({}));
	const auto manifest = readFileText(
		fixture().destination() / std::filesystem::path{kSessionDirectoryName} /
		std::filesystem::path{std::string{kManifestFileName}});
	EXPECT_NE(manifest.find(R"("originalName":"photos/deleted.jpg")"), std::string::npos);
	EXPECT_NE(
		manifest.find(revenant::toHex(revenant::sha256(fixtureContentNamed("deleted.jpg")))),
		std::string::npos);
	EXPECT_NE(manifest.find(R"("mode":"hybrid")"), std::string::npos);
}

// The whole run except the last step: everything is decided and recorded, and
// the destination is left exactly as it was found.
TEST_F(UndeleteCli, ADryRunDecidesEverythingAndWritesNothing) {
	ASSERT_TRUE(recover({"--dry-run"}));
	EXPECT_FALSE(std::filesystem::exists(fixture().recovered("photos")));
	EXPECT_FALSE(std::filesystem::exists(fixture().recovered("carved")));
	const auto manifest = readFileText(
		fixture().destination() / std::filesystem::path{kSessionDirectoryName} /
		std::filesystem::path{std::string{kManifestFileName}});
	EXPECT_NE(manifest.find(R"("writtenName":"photos/deleted.jpg")"), std::string::npos);
	EXPECT_NE(manifest.find(R"("outcome":"previewed")"), std::string::npos);
}

TEST_F(UndeleteCli, RefusesTwoModesThatContradictEachOther) {
	EXPECT_FALSE(recover({"--fs-only", "--carve-only"}));
}

TEST_F(UndeleteCli, RefusesAFlagItDoesNotKnow) {
	EXPECT_FALSE(recover({"--deep"}));
}

// The destination is checked before the scan, so a doomed run stops at once.
TEST_F(UndeleteCli, RefusesADestinationThatIsAFileRatherThanADirectory) {
	EXPECT_FALSE(runCli(
		runUndeleteCli,
		{"revenant-undelete",
		 "--source",
		 fixture().source().string(),
		 "--destination",
		 fixture().source().string()}));
}

TEST_F(UndeleteCli, RefusesASourceThatIsNotThere) {
	EXPECT_FALSE(runCli(
		runUndeleteCli,
		{"revenant-undelete",
		 "--source",
		 (fixture().destination() / "absent.img").string(),
		 "--destination",
		 fixture().destination().string()}));
}

TEST(UndeleteCliArguments, RefusesACommandLineWithNoArguments) {
	EXPECT_FALSE(runCli(runUndeleteCli, {"revenant-undelete"}));
}

TEST(UndeleteCliArguments, AnswersHelpWithTheUsageAndSucceeds) {
	EXPECT_TRUE(runCli(runUndeleteCli, {"revenant-undelete", "--help"}));
}

} // namespace

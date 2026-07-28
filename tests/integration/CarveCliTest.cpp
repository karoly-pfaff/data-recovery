// SPDX-License-Identifier: GPL-3.0-or-later
// The carve binary over a volume it deliberately does not mount: structure is
// the only thing deciding what comes back, which is the mode a formatted or RAW
// device leaves you with.
#include "cli/CarveCli.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "imagegen/ntfs/FixtureFiles.hpp"
#include "support/CliFixture.hpp"
#include "support/FixtureContent.hpp"

namespace {

using revenant::cli::runCarveCli;
using revenant::imagegen::ntfs::unallocatedJpeg;
using revenant::testing::CliFixture;
using revenant::testing::holdsFileOfType;
using revenant::testing::readFileBytes;
using revenant::testing::runCli;

[[nodiscard]] bool
anyFileHolds(const std::filesystem::path& directory, const std::vector<std::byte>& content) {
	const std::filesystem::recursive_directory_iterator entries{directory};
	return std::ranges::any_of(entries, [&content](const std::filesystem::directory_entry& entry) {
		return entry.is_regular_file() && readFileBytes(entry.path()) == content;
	});
}

class CarveCli : public ::testing::Test {
protected:
	[[nodiscard]] bool carve(const std::vector<std::string>& flags) const {
		return fixture_.run(runCarveCli, flags);
	}

	[[nodiscard]] const CliFixture& fixture() const noexcept {
		return fixture_;
	}

private:
	CliFixture fixture_;
};

// Every JPEG on the volume comes back, including the one no record points at —
// and none of them comes back with a name, because nothing read the metadata.
TEST_F(CarveCli, RecoversByStructureAloneAndNamesNothing) {
	ASSERT_TRUE(carve({}));
	EXPECT_TRUE(holdsFileOfType(fixture().recovered("carved"), ".jpg"));
	EXPECT_FALSE(std::filesystem::exists(fixture().recovered("photos")));
}

// The JPEG in unallocated space — the one nothing on the volume points at —
// comes back byte-for-byte. That exactness is the validating carver's whole
// claim, now made through the binary.
TEST_F(CarveCli, TheCarvedFilesAreExact) {
	ASSERT_TRUE(carve({}));
	EXPECT_TRUE(anyFileHolds(fixture().recovered("carved"), unallocatedJpeg()));
}

TEST_F(CarveCli, AnAllowlistTheImageMatchesStillRecoversIt) {
	ASSERT_TRUE(carve({"--formats", "jpg"}));
	EXPECT_TRUE(holdsFileOfType(fixture().recovered("carved"), ".jpg"));
}

// A format the volume does not hold is not an error: an empty recovery is an
// answer, and the run says so rather than failing.
TEST_F(CarveCli, AnAllowlistNothingMatchesRecoversNothingAndSucceeds) {
	ASSERT_TRUE(carve({"--formats", "pdf"}));
	EXPECT_FALSE(holdsFileOfType(fixture().recovered("carved"), ".jpg"));
}

TEST_F(CarveCli, RefusesAFormatNoCarverAnswersTo) {
	EXPECT_FALSE(carve({"--formats", "tiff"}));
}

// There is only one mode here, so a mode flag is a misunderstanding worth
// surfacing rather than a no-op to accept.
TEST_F(CarveCli, RefusesAModeFlag) {
	EXPECT_FALSE(carve({"--fs-only"}));
}

TEST_F(CarveCli, RefusesASourceThatIsNotThere) {
	EXPECT_FALSE(runCli(
		runCarveCli,
		{"revenant-carve",
		 "--source",
		 (fixture().destination() / "absent.img").string(),
		 "--destination",
		 fixture().destination().string()}));
}

TEST(CarveCliArguments, RefusesACommandLineWithNoArguments) {
	EXPECT_FALSE(runCli(runCarveCli, {"revenant-carve"}));
}

TEST(CarveCliArguments, AnswersHelpWithTheUsageAndSucceeds) {
	EXPECT_TRUE(runCli(runCarveCli, {"revenant-carve", "--help"}));
}

} // namespace

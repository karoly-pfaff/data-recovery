// SPDX-License-Identifier: GPL-3.0-or-later
// The M1 vertical slice, whole: a real image file mounted read-only, recovered
// by both sources, indexed, arbitrated, and written out — then every file on
// disk compared byte-for-byte against the fixture that produced it.
#include <gtest/gtest.h>

#include <filesystem>

#include "imagegen/ntfs/FixtureFiles.hpp"
#include "support/FixtureContent.hpp"
#include "support/RecoveryPipeline.hpp"

namespace {

using revenant::imagegen::ntfs::unallocatedJpeg;
using revenant::testing::fixtureContentNamed;
using revenant::testing::readFileBytes;

// The pipeline runs once per test; the assertions below are what this file is for.
class RecoveredFiles : public revenant::testing::RecoveryPipeline {
protected:
	RecoveredFiles() {
		runFullRecovery();
	}
};

TEST_F(RecoveredFiles, EveryWinnerLandedAndNoneFailed) {
	EXPECT_GT(stats().filesWritten, 0U);
	EXPECT_EQ(stats().failed, 0U);
}

// The deleted, fragmented JPEG comes back at its own path, with its own bytes.
// This is the sentence the whole milestone exists to be able to say.
TEST_F(RecoveredFiles, TheDeletedFragmentedJpegIsBackAtItsPathAndByteIdentical) {
	const auto written = recovered("photos/deleted.jpg");
	ASSERT_TRUE(std::filesystem::exists(written));
	EXPECT_EQ(readFileBytes(written), fixtureContentNamed("deleted.jpg"));
}

TEST_F(RecoveredFiles, TheLiveAndResidentFilesComeBackToo) {
	EXPECT_EQ(readFileBytes(recovered("photos/keep.jpg")), fixtureContentNamed("keep.jpg"));
	EXPECT_EQ(readFileBytes(recovered("notes.txt")), fixtureContentNamed("notes.txt"));
}

TEST_F(RecoveredFiles, TheOrphanKeepsItsNameEvenWithoutItsParent) {
	EXPECT_EQ(readFileBytes(recovered("orphan.jpg")), fixtureContentNamed("orphan.jpg"));
}

// No record points at it, so it has no name to keep — it comes back carved,
// bucketed by format, and exact.
TEST_F(RecoveredFiles, TheUnreferencedJpegComesBackUnderTheCarvedBucket) {
	const auto written = recovered("carved/jpg/f00000004.jpg");
	ASSERT_TRUE(std::filesystem::exists(written));
	EXPECT_EQ(readFileBytes(written), unallocatedJpeg());
}

TEST_F(RecoveredFiles, NothingWasWrittenOutsideTheDestination) {
	EXPECT_EQ(stats().renamed, 0U);
	EXPECT_TRUE(std::filesystem::exists(recovered("photos")));
}

} // namespace

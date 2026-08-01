// SPDX-License-Identifier: GPL-3.0-or-later
// story-0405: a whole disk, and a run aimed at one of its partitions. What is
// under test is that the window actually confines the run — the NTFS volume's
// files come back when partition 1 is named, and do not when partition 2 is.
// Nothing below the CLI knows partitions exist; a PartitionView is a BlockDevice
// like any other, which is the whole point of the seam.
#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "cli/RecoveryOptions.hpp"
#include "cli/UndeleteCli.hpp"
#include "imagegen/disk/DiskImageBuilder.hpp"
#include "support/CliFixture.hpp"
#include "support/FixtureContent.hpp"
#include "support/TempDir.hpp"
#include "support/TempFile.hpp"

namespace {

using revenant::cli::kSessionDirectoryName;
using revenant::cli::runUndeleteCli;
using revenant::imagegen::disk::buildMbrDiskImage;
using revenant::imagegen::disk::buildPhantomTableDiskImage;
using revenant::testing::fixtureContentNamed;
using revenant::testing::readFileBytes;
using revenant::testing::runCli;
using revenant::testing::TempDir;
using revenant::testing::TempFile;

// The NTFS fixture is partition 1 of the synthetic disk, and the file it holds
// under `photos/` is the one every NTFS test asserts on.
constexpr const char* kNtfsPartition = "1";
constexpr const char* kFat32Partition = "2";

// A whole disk on disk, plus somewhere to recover into.
class PartitionedDisk {
public:
	explicit PartitionedDisk(const std::vector<std::byte>& bytes) : image_(bytes) {}

	[[nodiscard]] bool undeleteFrom(const std::string& partition) const {
		return runCli(
			runUndeleteCli,
			{"revenant-undelete",
			 "--source",
			 image_.path().string(),
			 "--destination",
			 output_.path().string(),
			 "--partition",
			 partition});
	}

	[[nodiscard]] std::filesystem::path recovered(const std::string& relative) const {
		return output_.path() / relative;
	}

	// Every recovered artifact, as relative path to bytes. What "the same run"
	// means when two runs are compared to each other rather than to a fixture —
	// a file missing from either side is as much a difference as a file whose
	// bytes differ.
	//
	// The session directory is left out, and not for convenience: its manifest
	// records the source path, which is a different temporary file for each of
	// the two runs, so it can never match and says nothing about what was
	// recovered (story-0603 hit the same thing).
	[[nodiscard]] std::map<std::string, std::vector<std::byte>> artifacts() const {
		std::map<std::string, std::vector<std::byte>> found;
		for (const auto& entry : std::filesystem::recursive_directory_iterator{output_.path()}) {
			const auto relative = entry.path().lexically_relative(output_.path());
			if (entry.is_regular_file() && *relative.begin() != kSessionDirectoryName) {
				found.emplace(relative.generic_string(), readFileBytes(entry.path()));
			}
		}
		return found;
	}

private:
	TempFile image_;
	TempDir output_;
};

TEST(PartitionSelection, RecoversTheNamedPartitionsFiles) {
	const PartitionedDisk disk{buildMbrDiskImage().bytes};
	ASSERT_TRUE(disk.undeleteFrom(kNtfsPartition));
	EXPECT_EQ(
		readFileBytes(disk.recovered("photos/deleted.jpg")),
		fixtureContentNamed("deleted.jpg"));
}

// The same disk, a different window: the NTFS volume is outside it, so its
// files are not there to be found.
TEST(PartitionSelection, DoesNotReachIntoAnotherPartition) {
	const PartitionedDisk disk{buildMbrDiskImage().bytes};
	ASSERT_TRUE(disk.undeleteFrom(kFat32Partition));
	EXPECT_FALSE(std::filesystem::exists(disk.recovered("photos/deleted.jpg")));
}

// A partition the table does not describe is a refusal, not a whole-disk run:
// recovering the wrong range is worse than recovering nothing.
TEST(PartitionSelection, RefusesAPartitionTheTableDoesNotHave) {
	const PartitionedDisk disk{buildMbrDiskImage().bytes};
	EXPECT_FALSE(disk.undeleteFrom("9"));
}

// story-0610: the same run, over a disk whose NTFS volume carries bytes that
// parse as a partition table — which is what a real volume's bootstrap area is.
// The operator named the partition, so the scope is settled; a run that goes
// looking for a table inside it finds the phantom, mounts nothing, and reports
// a healthy volume with no files in it.
//
// Both runs are compared to each other in full rather than one file each: the
// old failure produced *some* output, so a single named file is what a phantom
// run could still get right by carving.
TEST(PartitionSelection, RecoversFromAVolumeWhoseOwnSectorParsesAsATable) {
	const PartitionedDisk clean{buildMbrDiskImage().bytes};
	ASSERT_TRUE(clean.undeleteFrom(kNtfsPartition));
	const PartitionedDisk phantom{buildPhantomTableDiskImage().bytes};
	ASSERT_TRUE(phantom.undeleteFrom(kNtfsPartition));

	const auto expected = clean.artifacts();
	ASSERT_FALSE(expected.empty());
	EXPECT_EQ(phantom.artifacts(), expected);
	EXPECT_EQ(
		readFileBytes(phantom.recovered("photos/deleted.jpg")),
		fixtureContentNamed("deleted.jpg"));
}

} // namespace

// SPDX-License-Identifier: GPL-3.0-or-later
// The story-0029 seam: one factory decides which filesystem a volume carries,
// and hands back something that can be walked without naming NTFS. What is
// asserted here is the *contract* between the factory and its mounters —
// declining, owning, and failing — not what any one parser does with the bytes.
#include "revenant/fs/Mount.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "support/CollectingEntryVisitor.hpp"
#include "support/InMemoryDevice.hpp"
#include "support/NtfsVolume.hpp"

namespace {

using revenant::BlockDevice;
using revenant::Error;
using revenant::ErrorCode;
using revenant::Result;
using revenant::fs::FileSystem;
using revenant::fs::mountVolume;
using revenant::fs::RecoveredEntry;
using revenant::testing::CollectingEntryVisitor;
using revenant::testing::InMemoryDevice;
using revenant::testing::NtfsVolume;
using revenant::testing::VolumeRange;

constexpr std::uint32_t kSectorSize = 512;
constexpr std::size_t kTooShortForABootSector = 64;

// The boot sector's OEM id is the field NTFS names itself in. Zeroing it is how
// a volume stops being anyone's; zeroing a field *after* it leaves the name
// intact and the geometry broken, which is the other half of the contract.
constexpr VolumeRange kOemId{.offset = 0x03, .length = 8};
constexpr VolumeRange kBytesPerSector{.offset = 0x0B, .length = 2};

// A device that faults on every read — a disk that will not answer, which is
// not the same fact as a disk carrying no filesystem.
class UnreadableDevice final : public BlockDevice {
public:
	[[nodiscard]] std::uint64_t sizeInBytes() const override {
		return std::uint64_t{1} << 20U;
	}

	[[nodiscard]] std::uint32_t sectorSize() const override {
		return kSectorSize;
	}

	[[nodiscard]] Result<std::size_t>
	readAt(std::uint64_t offset, std::span<std::byte> /*buffer*/) override {
		return Error{.code = ErrorCode::kIoFailure, .offset = offset};
	}
};

[[nodiscard]] ErrorCode mountErrorOf(BlockDevice& device) {
	const auto mounted = mountVolume(device);
	EXPECT_FALSE(mounted.hasValue());
	return mounted.hasValue() ? ErrorCode::kNotFound : mounted.error().code;
}

[[nodiscard]] std::vector<std::string> pathsIn(const std::vector<RecoveredEntry>& entries) {
	std::vector<std::string> paths;
	paths.reserve(entries.size());
	for (const auto& entry : entries) {
		paths.push_back(entry.path);
	}
	return paths;
}

// One walk of a mounted volume, as the paths it reported.
[[nodiscard]] std::vector<std::string> walkPaths(const FileSystem& volume) {
	CollectingEntryVisitor visitor;
	EXPECT_TRUE(volume.enumerate(visitor).hasValue());
	return pathsIn(visitor.entries());
}

// What "the seam changed no answers" has to be measured against.
[[nodiscard]] std::vector<std::string> pathsOf(BlockDevice& device) {
	const auto mounted = mountVolume(device);
	EXPECT_TRUE(mounted.hasValue());
	return mounted.hasValue() ? walkPaths(*mounted.value()) : std::vector<std::string>{};
}

TEST(MountVolume, MountsTheFilesystemTheVolumeCarries) {
	NtfsVolume volume;
	const auto mounted = mountVolume(volume.mount());
	ASSERT_TRUE(mounted.hasValue());
	EXPECT_NE(mounted.value(), nullptr);
}

TEST(MountVolume, TheMountedVolumeWalksToTheEntriesItHolds) {
	NtfsVolume volume;
	CollectingEntryVisitor visitor;
	const auto mounted = mountVolume(volume.mount());
	ASSERT_TRUE(mounted.hasValue());
	const auto stats = mounted.value()->enumerate(visitor);
	ASSERT_TRUE(stats.hasValue());
	EXPECT_EQ(stats.value().entriesReported, 4U);
	EXPECT_EQ(visitor.entries().size(), stats.value().entriesReported);
}

// The fixture's four files, by name — the seam is a new way in to the same
// walk, not a new answer.
TEST(MountVolume, ReportsTheSameEntriesTheDirectWalkDoes) {
	NtfsVolume volume;
	EXPECT_EQ(
		pathsOf(volume.mount()),
		(std::vector<std::string>{
			"photos/keep.jpg",
			"photos/deleted.jpg",
			"notes.txt",
			"orphan.jpg"}));
}

// A formatted or RAW volume: nothing recognized it. That is a different fact
// from "this NTFS volume is broken", and it is the one carving exists for.
TEST(MountVolume, AVolumeNoFilesystemRecognizesIsNotFound) {
	NtfsVolume volume;
	volume.clear(kOemId);
	EXPECT_EQ(mountErrorOf(volume.mount()), ErrorCode::kNotFound);
}

// The other half: a mounter that recognizes its own signature owns the answer,
// so its parse failure is reported rather than passed to the next filesystem.
TEST(MountVolume, ARecognizedVolumeThatWillNotParseKeepsItsOwnError) {
	NtfsVolume volume;
	volume.clear(kBytesPerSector);
	EXPECT_EQ(mountErrorOf(volume.mount()), ErrorCode::kInvalidArgument);
}

TEST(MountVolume, ADeviceTooShortToHoldABootSectorCarriesNoFilesystem) {
	InMemoryDevice tiny{std::vector<std::byte>(kTooShortForABootSector, std::byte{0}), kSectorSize};
	EXPECT_EQ(mountErrorOf(tiny), ErrorCode::kNotFound);
}

// A disk that will not read is not a disk with no files on it.
TEST(MountVolume, ADeviceThatFaultsReportsTheFaultRatherThanNoFilesystem) {
	UnreadableDevice device;
	EXPECT_EQ(mountErrorOf(device), ErrorCode::kIoFailure);
}

} // namespace

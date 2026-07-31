// SPDX-License-Identifier: GPL-3.0-or-later
// story-0609: ADR-0005's destination rule, both tiers. The verdict over
// physical storage is driven by identities handed straight in, because no CI
// runner hands out a disk to point the resolvers at — and a rule that can only
// be exercised against real hardware is a rule nobody checks.
#include "recovery/DestinationRule.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/DeviceIdentity.hpp"
#include "support/TempDir.hpp"
#include "support/TempFile.hpp"

namespace {

using revenant::Error;
using revenant::ErrorCode;
using revenant::Result;
using revenant::StorageExtent;
using revenant::StorageExtents;
using revenant::recovery::destinationOnSource;
using revenant::recovery::refuseOverlap;
using revenant::testing::TempDir;
using revenant::testing::TempFile;

constexpr std::uint64_t kGiB = std::uint64_t{1} << 30U;
constexpr std::int32_t kOsReason = 5;
constexpr std::size_t kImageBytes = 512;

[[nodiscard]] StorageExtents wholeDisk(std::uint64_t disk) {
	return {StorageExtent{.disk = disk, .offsetBytes = 0, .lengthBytes = revenant::kWholeDisk}};
}

[[nodiscard]] StorageExtents volumeOn(std::uint64_t disk, std::uint64_t startBytes) {
	return {StorageExtent{.disk = disk, .offsetBytes = startBytes, .lengthBytes = 100 * kGiB}};
}

[[nodiscard]] Result<StorageExtents> unresolvable() {
	return Error{.code = ErrorCode::kIoFailure, .offset = 0, .osCode = kOsReason};
}

TEST(DestinationRule, RefusesADestinationOnTheSourcesStorage) {
	const auto refusal = refuseOverlap(wholeDisk(0), volumeOn(0, kGiB));
	ASSERT_TRUE(refusal.has_value());
	EXPECT_EQ(refusal->code, ErrorCode::kDestinationOnSource);
}

TEST(DestinationRule, AllowsADestinationOnStorageTheSourceDoesNotTouch) {
	EXPECT_FALSE(refuseOverlap(wholeDisk(0), volumeOn(1, kGiB)).has_value());
}

// When the check cannot prove the destination is elsewhere, "elsewhere" is not
// assumed: the run is refused, carrying the OS's reason for not being able to
// tell.
TEST(DestinationRule, RefusesWhenTheSourcesStorageCannotBeResolved) {
	const auto refusal = refuseOverlap(unresolvable(), volumeOn(1, kGiB));
	ASSERT_TRUE(refusal.has_value());
	EXPECT_EQ(refusal->code, ErrorCode::kDestinationOnSource);
	EXPECT_EQ(refusal->osCode, kOsReason);
}

TEST(DestinationRule, RefusesWhenTheDestinationsStorageCannotBeResolved) {
	const auto refusal = refuseOverlap(wholeDisk(0), unresolvable());
	ASSERT_TRUE(refusal.has_value());
	EXPECT_EQ(refusal->osCode, kOsReason);
}

// An image-file source keeps the rule story-0109 wrote: the output tree must
// not grow around the image it is reading.
TEST(DestinationRule, RefusesADestinationHoldingTheImageBeingRead) {
	const TempDir directory;
	const auto refusal = destinationOnSource(directory.path(), directory.path() / "disk.img");
	ASSERT_TRUE(refusal.has_value());
	EXPECT_EQ(refusal->code, ErrorCode::kInvalidArgument);
}

// A destination sharing a volume with a disk image is normal practice, not a
// loss mode. This also proves identity is never consulted for an image: a
// regular file has no storage to read, so a rule that asked would refuse here.
TEST(DestinationRule, AllowsADestinationBesideTheImageBeingRead) {
	const TempDir directory;
	const TempFile image{std::vector<std::byte>(kImageBytes, std::byte{0})};
	EXPECT_FALSE(destinationOnSource(directory.path(), image.path()).has_value());
}

} // namespace

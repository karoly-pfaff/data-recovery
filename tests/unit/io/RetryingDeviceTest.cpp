// SPDX-License-Identifier: GPL-3.0-or-later
// story-0402: surviving a drive that will not answer. Every test here passes a
// zero pause — the wait is for real hardware's error recovery, and a test that
// slept for it would buy nothing but wall clock.
#include "revenant/core/io/RetryingDevice.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/io/BadRange.hpp"
#include "revenant/core/io/CachingDevice.hpp"
#include "support/FaultyDevice.hpp"

namespace {

using revenant::BadRange;
using revenant::CacheShape;
using revenant::CachingDevice;
using revenant::RetryingDevice;
using revenant::RetryPolicy;
using revenant::testing::Fault;
using revenant::testing::FaultyDevice;

constexpr std::uint32_t kSector = 512;
// The same number, widened once, for the arithmetic below: a product of two
// 32-bit values assigned to a 64-bit one is a widening clang-tidy objects to.
constexpr std::size_t kSectorBytes = kSector;
constexpr std::size_t kDeviceBytes = 4096;
constexpr RetryPolicy kNoWaiting{.attempts = 3, .pause = std::chrono::milliseconds{0}};

[[nodiscard]] std::vector<std::byte> countingBytes(std::size_t count) {
	std::vector<std::byte> bytes(count);
	for (std::size_t at = 0; at < count; ++at) {
		bytes.at(at) = static_cast<std::byte>(static_cast<std::uint8_t>((at % 251) + 1));
	}
	return bytes;
}

[[nodiscard]] bool allZero(std::span<const std::byte> bytes) {
	return std::ranges::all_of(bytes, [](std::byte value) { return value == std::byte{0}; });
}

// A drive that fails a request once and reads it on the retry — the transient
// fault the whole-request retry exists for.
TEST(RetryingDevice, ReadsWhatASecondAttemptGivesUp) {
	FaultyDevice source{
		countingBytes(kDeviceBytes),
		kSector,
		{Fault{.offsetBytes = 0, .lengthBytes = kDeviceBytes, .refusals = 1, .permanent = false}}};
	RetryingDevice device{source, kNoWaiting};
	std::vector<std::byte> buffer(kSector);
	const auto read = device.readAt(0, buffer);
	ASSERT_TRUE(read.hasValue());
	EXPECT_EQ(read.value(), kSector);
	EXPECT_EQ(source.reads(), 2U);
	EXPECT_TRUE(device.badRanges().empty());
}

TEST(RetryingDevice, RecordsNothingWhenNothingFails) {
	FaultyDevice source{countingBytes(kDeviceBytes), kSector, {}};
	RetryingDevice device{source, kNoWaiting};
	std::vector<std::byte> buffer(1024);
	ASSERT_TRUE(device.readAt(0, buffer).hasValue());
	EXPECT_TRUE(device.badRanges().empty());
}

// A hard fault covers a few sectors, not the request that spans them: the
// sectors around it are still the device's own bytes.
TEST(RetryingDevice, KeepsTheGoodSectorsAroundAHardFault) {
	const auto content = countingBytes(kDeviceBytes);
	FaultyDevice source{content, kSector, {Fault{.offsetBytes = kSector, .lengthBytes = kSector}}};
	RetryingDevice device{source, kNoWaiting};
	std::vector<std::byte> buffer(3 * kSectorBytes);
	const auto read = device.readAt(0, buffer);
	ASSERT_TRUE(read.hasValue());
	ASSERT_EQ(read.value(), 3U * kSectorBytes);
	EXPECT_TRUE(
		std::ranges::equal(std::span{buffer}.first(kSector), std::span{content}.first(kSector)));
	EXPECT_TRUE(
		std::ranges::equal(
			std::span{buffer}.subspan(2 * kSectorBytes, kSector),
			std::span{content}.subspan(2 * kSectorBytes, kSector)));
}

TEST(RetryingDevice, HandsBackZerosForWhatItCannotRead) {
	FaultyDevice source{
		countingBytes(kDeviceBytes),
		kSector,
		{Fault{.offsetBytes = kSector, .lengthBytes = kSector}}};
	RetryingDevice device{source, kNoWaiting};
	std::vector<std::byte> buffer(3 * kSectorBytes);
	ASSERT_TRUE(device.readAt(0, buffer).hasValue());
	EXPECT_TRUE(allZero(std::span{buffer}.subspan(kSector, kSector)));
}

TEST(RetryingDevice, RecordsTheRangeItInvented) {
	FaultyDevice source{
		countingBytes(kDeviceBytes),
		kSector,
		{Fault{.offsetBytes = kSector, .lengthBytes = kSector}}};
	RetryingDevice device{source, kNoWaiting};
	std::vector<std::byte> buffer(3 * kSectorBytes);
	ASSERT_TRUE(device.readAt(0, buffer).hasValue());
	ASSERT_EQ(device.badRanges().size(), 1U);
	EXPECT_EQ(
		device.badRanges().front(),
		(BadRange{.offsetBytes = kSector, .lengthBytes = kSector}));
}

// A long bad run must not become one record per sector (ADR-0009).
TEST(RetryingDevice, MergesAdjacentBadSectorsIntoOneRange) {
	FaultyDevice source{
		countingBytes(kDeviceBytes),
		kSector,
		{Fault{.offsetBytes = 0, .lengthBytes = 3 * kSectorBytes}}};
	RetryingDevice device{source, kNoWaiting};
	std::vector<std::byte> buffer(3 * kSectorBytes);
	ASSERT_TRUE(device.readAt(0, buffer).hasValue());
	ASSERT_EQ(device.badRanges().size(), 1U);
	EXPECT_EQ(device.badRanges().front().lengthBytes, 3U * kSectorBytes);
}

// story-0604: the map is a set of damaged ranges, not a log of the reads that
// met them. A real run reads the same sector twice — once to scan it, once to
// extract from it — and recording each encounter would report twice the damage
// there is, in the byte total, in the manifest, and against every artifact that
// spans it.
TEST(RetryingDevice, ReadingTheSameBadSectorTwiceRecordsItOnce) {
	FaultyDevice source{
		countingBytes(kDeviceBytes),
		kSector,
		{Fault{.offsetBytes = kSector, .lengthBytes = kSector}}};
	RetryingDevice device{source, kNoWaiting};
	std::vector<std::byte> buffer(kSectorBytes);
	ASSERT_TRUE(device.readAt(kSector, buffer).hasValue());
	ASSERT_TRUE(device.readAt(kSector, buffer).hasValue());
	ASSERT_EQ(device.badRanges().size(), 1U);
	EXPECT_EQ(
		device.badRanges().front(),
		(BadRange{.offsetBytes = kSector, .lengthBytes = kSector}));
}

// Reads do not arrive in offset order — extraction follows a file's extents,
// not the disk — so a range met later but lying earlier still lands in place
// and still merges with what it touches.
TEST(RetryingDevice, MergesBadSectorsMetOutOfOrder) {
	FaultyDevice source{
		countingBytes(kDeviceBytes),
		kSector,
		{Fault{.offsetBytes = 0, .lengthBytes = 2 * kSectorBytes}}};
	RetryingDevice device{source, kNoWaiting};
	std::vector<std::byte> buffer(kSectorBytes);
	ASSERT_TRUE(device.readAt(kSector, buffer).hasValue());
	ASSERT_TRUE(device.readAt(0, buffer).hasValue());
	ASSERT_EQ(device.badRanges().size(), 1U);
	EXPECT_EQ(
		device.badRanges().front(),
		(BadRange{.offsetBytes = 0, .lengthBytes = 2 * kSectorBytes}));
}

// A range already in the map must not shrink when a narrower record lands
// inside it. The map only ever grows, because it is what the run reports as
// damaged — and under-reporting damage is the silence this whole story exists
// to remove. Two reads of different widths over one bad run produce exactly
// this: the wide one records three sectors, the narrow one records the first.
TEST(RetryingDevice, ARecordContainedInOneAlreadyHeldDoesNotShrinkIt) {
	FaultyDevice source{
		countingBytes(kDeviceBytes),
		kSector,
		{Fault{.offsetBytes = 0, .lengthBytes = 3 * kSectorBytes}}};
	RetryingDevice device{source, kNoWaiting};
	std::vector<std::byte> wide(3 * kSectorBytes);
	ASSERT_TRUE(device.readAt(0, wide).hasValue());
	std::vector<std::byte> narrow(kSectorBytes);
	ASSERT_TRUE(device.readAt(0, narrow).hasValue());
	ASSERT_EQ(device.badRanges().size(), 1U);
	EXPECT_EQ(device.badRanges().front().lengthBytes, 3U * kSectorBytes);
}

// story-0605: past a bound, unbroken damage is not damage. A device that has
// gone away refuses every sector, and zero-filling each one in turn would
// transcribe the whole corpse as zeros — the run has to be told the source is
// gone instead.
TEST(RetryingDevice, AnUnbrokenRunOfDamagePastTheBoundIsALostSource) {
	const auto bytes = static_cast<std::size_t>(revenant::kLostSourceRunBytes) + kSectorBytes;
	FaultyDevice source{
		countingBytes(bytes),
		kSector,
		{Fault{.offsetBytes = 0, .lengthBytes = bytes}}};
	RetryingDevice device{source, kNoWaiting};
	std::vector<std::byte> buffer(bytes);
	const auto read = device.readAt(0, buffer);
	ASSERT_FALSE(read.hasValue());
	EXPECT_EQ(read.error().code, revenant::ErrorCode::kSourceLost);
	EXPECT_EQ(read.error().offset, revenant::kLostSourceRunBytes);
}

// A patch is not a device. Damage well inside the bound is still zero-filled,
// recorded, and stepped over — which is the behaviour every other test here
// depends on and the one this bound must not break.
TEST(RetryingDevice, DamageInsideTheBoundIsStillJustDamage) {
	const auto bytes = static_cast<std::size_t>(revenant::kLostSourceRunBytes) * 2;
	FaultyDevice source{
		countingBytes(bytes),
		kSector,
		{Fault{.offsetBytes = 0, .lengthBytes = revenant::kLostSourceRunBytes / 2}}};
	RetryingDevice device{source, kNoWaiting};
	std::vector<std::byte> buffer(bytes);
	const auto read = device.readAt(0, buffer);
	ASSERT_TRUE(read.hasValue());
	EXPECT_EQ(read.value(), bytes);
	ASSERT_EQ(device.badRanges().size(), 1U);
	EXPECT_EQ(device.badRanges().front().lengthBytes, revenant::kLostSourceRunBytes / 2);
}

// A good sector between two bad patches is what says the device is still there.
TEST(RetryingDevice, AGoodSectorBetweenTwoPatchesKeepsTheSourceAlive) {
	const auto half = static_cast<std::size_t>(revenant::kLostSourceRunBytes) - kSectorBytes;
	const auto bytes = (2 * half) + (2 * kSectorBytes);
	FaultyDevice source{
		countingBytes(bytes),
		kSector,
		{Fault{.offsetBytes = 0, .lengthBytes = half},
		 Fault{.offsetBytes = half + kSectorBytes, .lengthBytes = half}}};
	RetryingDevice device{source, kNoWaiting};
	std::vector<std::byte> buffer(bytes);
	const auto read = device.readAt(0, buffer);
	ASSERT_TRUE(read.hasValue());
	EXPECT_EQ(read.value(), bytes);
	EXPECT_EQ(device.badRanges().size(), 2U);
}

// The two decorators compose, which is why they are decorators: the cache reads
// whole blocks, the retry layer under it survives the sectors that will not come.
TEST(RetryingDevice, ComposesUnderACache) {
	const auto content = countingBytes(kDeviceBytes);
	FaultyDevice source{content, kSector, {Fault{.offsetBytes = 1024, .lengthBytes = kSector}}};
	RetryingDevice retrying{source, kNoWaiting};
	CachingDevice cache{retrying, CacheShape{.blockBytes = 2048, .blockCount = 2}};
	std::vector<std::byte> buffer(2048);
	const auto read = cache.readAt(0, buffer);
	ASSERT_TRUE(read.hasValue());
	ASSERT_EQ(read.value(), 2048U);
	EXPECT_TRUE(std::ranges::equal(std::span{buffer}.first(1024), std::span{content}.first(1024)));
	EXPECT_TRUE(allZero(std::span{buffer}.subspan(1024, kSector)));
}

} // namespace

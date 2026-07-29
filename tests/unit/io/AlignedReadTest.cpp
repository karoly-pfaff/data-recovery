// SPDX-License-Identifier: GPL-3.0-or-later
// story-0401: the one part of a raw device that can be tested without a disk —
// and the part most likely to be wrong. The reader these drive against
// *refuses* anything unaligned, exactly as a real raw device does, so a slicing
// mistake fails here rather than on someone's failing drive.
#include "revenant/core/io/AlignedRead.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace {

using revenant::alignedWindow;
using revenant::ByteRange;
using revenant::ErrorCode;
using revenant::readThroughAlignment;
using revenant::Result;

constexpr std::uint32_t kSector = 512;

[[nodiscard]] std::vector<std::byte> countingBytes(std::size_t count) {
	std::vector<std::byte> bytes(count);
	for (std::size_t at = 0; at < count; ++at) {
		bytes.at(at) = static_cast<std::byte>(static_cast<std::uint8_t>((at % 251) + 1));
	}
	return bytes;
}

// A device that answers only whole-sector reads, and counts them. A real raw
// device is exactly this strict, so anything that passes here passes there.
// `endsAt` is where its readable bytes stop — a read reaching past it comes back
// short, which is the case the slicing has to get right.
class AlignedOnlyReader {
public:
	AlignedOnlyReader(std::vector<std::byte> data, std::size_t endsAt)
		: data_(std::move(data)), endsAt_(std::min(endsAt, data_.size())) {}

	[[nodiscard]] Result<std::size_t> operator()(std::uint64_t offset, std::span<std::byte> into) {
		++reads_;
		lastLength_ = into.size();
		if (offset % kSector != 0 || into.size() % kSector != 0) {
			return revenant::Error{.code = ErrorCode::kInvalidArgument, .offset = offset};
		}
		return supply(offset, into);
	}

	[[nodiscard]] std::size_t supply(std::uint64_t offset, std::span<std::byte> into) {
		if (offset >= endsAt_) {
			return 0;
		}
		const auto count = std::min(into.size(), endsAt_ - static_cast<std::size_t>(offset));
		std::copy_n(data_.begin() + static_cast<std::ptrdiff_t>(offset), count, into.begin());
		return count;
	}

	[[nodiscard]] std::uint32_t reads() const noexcept {
		return reads_;
	}

	[[nodiscard]] std::size_t lastLength() const noexcept {
		return lastLength_;
	}

private:
	std::vector<std::byte> data_;
	std::size_t endsAt_;
	std::uint32_t reads_ = 0;
	std::size_t lastLength_ = 0;
};

// A reader that answers nothing, to prove a fault travels.
[[nodiscard]] Result<std::size_t> failingReader(std::uint64_t offset, std::span<std::byte> into) {
	static_cast<void>(into);
	return revenant::Error{.code = ErrorCode::kIoFailure, .offset = offset};
}

TEST(AlignedWindowFor, LeavesAnAlreadyAlignedRangeAlone) {
	const auto window = alignedWindow(ByteRange{.offset = 1024, .length = 512}, kSector);
	EXPECT_EQ(window.offset, 1024U);
	EXPECT_EQ(window.length, 512U);
	EXPECT_EQ(window.skip, 0U);
}

TEST(AlignedWindowFor, RoundsAnOffsetDownAndRecordsHowFarIn) {
	const auto window = alignedWindow(ByteRange{.offset = 1030, .length = 8}, kSector);
	EXPECT_EQ(window.offset, 1024U);
	EXPECT_EQ(window.length, 512U);
	EXPECT_EQ(window.skip, 6U);
}

TEST(AlignedWindowFor, RoundsALengthUpToCoverTheRequest) {
	const auto window = alignedWindow(ByteRange{.offset = 0, .length = 513}, kSector);
	EXPECT_EQ(window.offset, 0U);
	EXPECT_EQ(window.length, 1024U);
}

TEST(AlignedWindowFor, CoversARangeThatSpansThreeSectorsFromTheMiddleOfOne) {
	const auto window = alignedWindow(ByteRange{.offset = 500, .length = 1100}, kSector);
	EXPECT_EQ(window.offset, 0U);
	EXPECT_EQ(window.length, 1600U + 448U);
	EXPECT_EQ(window.skip, 500U);
}

// A sector size of zero cannot describe a device, and dividing by it would be
// undefined rather than merely wrong.
TEST(AlignedWindowFor, TreatsAZeroSectorSizeAsOne) {
	const auto window = alignedWindow(ByteRange{.offset = 7, .length = 3}, 0);
	EXPECT_EQ(window.offset, 7U);
	EXPECT_EQ(window.length, 3U);
	EXPECT_EQ(window.skip, 0U);
}

TEST(ReadThroughAlignment, PassesAnAlignedRequestStraightThrough) {
	AlignedOnlyReader reader{countingBytes(4096), 4096};
	std::vector<std::byte> buffer(1024);
	const auto read = readThroughAlignment(512, buffer, kSector, std::ref(reader));
	ASSERT_TRUE(read.hasValue());
	EXPECT_EQ(read.value(), 1024U);
	EXPECT_EQ(reader.reads(), 1U);
	EXPECT_EQ(reader.lastLength(), 1024U);
}

TEST(ReadThroughAlignment, ServesAnUnalignedRequestFromAnAlignedWindow) {
	const auto content = countingBytes(4096);
	AlignedOnlyReader reader{content, 4096};
	std::vector<std::byte> buffer(100);
	const auto read = readThroughAlignment(600, buffer, kSector, std::ref(reader));
	ASSERT_TRUE(read.hasValue());
	ASSERT_EQ(read.value(), 100U);
	EXPECT_TRUE(std::ranges::equal(buffer, std::span{content}.subspan(600, 100)));
}

// The window begins before the caller's offset, so a short read has to be
// measured from `skip` — counting it from zero would hand back bounce-buffer
// bytes nobody wrote.
TEST(ReadThroughAlignment, MeasuresAShortReadFromWhereTheCallersBytesBegin) {
	const auto content = countingBytes(4096);
	AlignedOnlyReader reader{content, 700};
	std::vector<std::byte> buffer(400);
	const auto read = readThroughAlignment(600, buffer, kSector, std::ref(reader));
	ASSERT_TRUE(read.hasValue());
	ASSERT_EQ(read.value(), 100U);
	EXPECT_TRUE(
		std::ranges::equal(std::span{buffer}.first(100), std::span{content}.subspan(600, 100)));
}

TEST(ReadThroughAlignment, GivesNothingWhenTheReadStoppedBeforeTheCallersRange) {
	AlignedOnlyReader reader{countingBytes(4096), 560};
	std::vector<std::byte> buffer(64);
	const auto read = readThroughAlignment(600, buffer, kSector, std::ref(reader));
	ASSERT_TRUE(read.hasValue());
	EXPECT_EQ(read.value(), 0U);
}

TEST(ReadThroughAlignment, PassesADeviceFaultThrough) {
	std::vector<std::byte> buffer(64);
	const auto read = readThroughAlignment(600, buffer, kSector, failingReader);
	ASSERT_FALSE(read.hasValue());
	EXPECT_EQ(read.error().code, ErrorCode::kIoFailure);
}

} // namespace

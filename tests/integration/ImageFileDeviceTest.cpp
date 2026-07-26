// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/io/ImageFileDevice.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "support/TempFile.hpp"

namespace {

using revenant::ErrorCode;
using revenant::ImageFileDevice;
using revenant::testing::TempFile;

constexpr std::size_t kImageBytes = 8192;

std::vector<std::byte> patternBytes(std::size_t count) {
	std::vector<std::byte> bytes(count);
	for (std::size_t i = 0; i < count; ++i) {
		bytes.at(i) = static_cast<std::byte>((i * 7U) & 0xFFU);
	}
	return bytes;
}

TEST(ImageFileDevice, ReadsExactBytesFromImage) {
	const TempFile image{patternBytes(kImageBytes)};
	auto device = ImageFileDevice::open(image.path());
	ASSERT_TRUE(device.hasValue());
	std::vector<std::byte> buffer(256);
	ASSERT_EQ(device.value()->readAt(4096, buffer).value(), 256U);
	EXPECT_EQ(buffer.at(0), static_cast<std::byte>((4096U * 7U) & 0xFFU));
}

TEST(ImageFileDevice, ReportsFileSizeAndDefaultSector) {
	const TempFile image{patternBytes(kImageBytes)};
	const auto device = ImageFileDevice::open(image.path());
	ASSERT_TRUE(device.hasValue());
	EXPECT_EQ(device.value()->sizeInBytes(), kImageBytes);
	EXPECT_EQ(device.value()->sectorSize(), revenant::kDefaultSectorSize);
}

TEST(ImageFileDevice, SectorSizeIsOverridable) {
	const TempFile image{patternBytes(kImageBytes)};
	const auto device = ImageFileDevice::open(image.path(), 4096);
	ASSERT_TRUE(device.hasValue());
	EXPECT_EQ(device.value()->sectorSize(), 4096U);
}

TEST(ImageFileDevice, ZeroSectorSizeIsInvalidArgument) {
	const TempFile image{patternBytes(kImageBytes)};
	EXPECT_EQ(ImageFileDevice::open(image.path(), 0).error().code, ErrorCode::kInvalidArgument);
}

TEST(ImageFileDevice, TailReadIsShort) {
	const TempFile image{patternBytes(kImageBytes)};
	auto device = ImageFileDevice::open(image.path());
	std::vector<std::byte> buffer(512);
	EXPECT_EQ(device.value()->readAt(kImageBytes - 100, buffer).value(), 100U);
}

TEST(ImageFileDevice, MissingFileIsTypedNotFound) {
	const auto device = ImageFileDevice::open("Z:/no/such/revenant-image.dd");
	ASSERT_FALSE(device.hasValue());
	EXPECT_EQ(device.error().code, ErrorCode::kNotFound);
	EXPECT_NE(device.error().osCode, 0);
}

void readSlice(revenant::BlockDevice& device, std::size_t sliceIndex, bool& correct) {
	std::vector<std::byte> buffer(1024);
	const auto got = device.readAt(sliceIndex * 1024U, buffer);
	correct = got.hasValue() && got.value() == 1024U &&
			  buffer.at(0) == static_cast<std::byte>((sliceIndex * 1024U * 7U) & 0xFFU);
}

TEST(ImageFileDevice, ConcurrentReadsDoNotInterleave) {
	const TempFile image{patternBytes(kImageBytes)};
	auto device = ImageFileDevice::open(image.path());
	std::vector<std::thread> threads;
	std::array<bool, 4> results{};
	threads.reserve(results.size());
	for (std::size_t i = 0; i < results.size(); ++i) {
		threads.emplace_back(readSlice, std::ref(*device.value()), i, std::ref(results.at(i)));
	}
	for (auto& thread : threads) {
		thread.join();
	}
	EXPECT_TRUE(results.at(0) && results.at(1) && results.at(2) && results.at(3));
}

} // namespace

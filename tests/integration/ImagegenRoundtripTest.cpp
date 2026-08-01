// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include "imagegen/PatternWriter.hpp"
#include "revenant/core/Endian.hpp"
#include "revenant/core/io/ImageFileDevice.hpp"
#include "support/FixtureContent.hpp"

namespace {

using revenant::ImageFileDevice;
using revenant::imagegen::kSectorBytes;
using revenant::imagegen::Pattern;
using revenant::imagegen::writeImage;
using revenant::testing::readFileBytes;

constexpr std::uint64_t kImageBytes = 16 * kSectorBytes;

std::filesystem::path tempImagePath(const std::string& name) {
	return std::filesystem::temp_directory_path() / ("revenant-imagegen-" + name + ".img");
}

TEST(ImagegenRoundtrip, LbaTagsReadBackThroughImageFileDevice) {
	const auto path = tempImagePath("roundtrip");
	ASSERT_EQ(writeImage(path, kImageBytes, Pattern::kLbaTag).value(), kImageBytes);
	auto device = ImageFileDevice::open(path);
	ASSERT_TRUE(device.hasValue());
	std::vector<std::byte> sector(kSectorBytes);
	ASSERT_EQ(device.value()->readAt(7 * kSectorBytes, sector).value(), kSectorBytes);
	EXPECT_EQ(
		revenant::fromLittleEndian<std::uint64_t>(std::span<const std::byte, 8>{sector.data(), 8}),
		7U);
	// Windows opens the image without FILE_SHARE_DELETE (ImageFileDeviceWindows.cpp);
	// release the handle before removing the file, or the remove below throws a
	// sharing-violation exception. (Authorized deviation from the brief's verbatim
	// text — see task-7-report.md.)
	device.value().reset();
	std::filesystem::remove(path);
}

TEST(ImagegenRoundtrip, GenerationIsDeterministic) {
	const auto first = tempImagePath("det-a");
	const auto second = tempImagePath("det-b");
	ASSERT_TRUE(writeImage(first, kImageBytes, Pattern::kCounter).hasValue());
	ASSERT_TRUE(writeImage(second, kImageBytes, Pattern::kCounter).hasValue());
	EXPECT_EQ(readFileBytes(first), readFileBytes(second));
	std::filesystem::remove(first);
	std::filesystem::remove(second);
}

TEST(ImagegenRoundtrip, PartialTailSectorIsWritten) {
	const auto path = tempImagePath("tail");
	ASSERT_EQ(writeImage(path, kSectorBytes + 100, Pattern::kZero).value(), kSectorBytes + 100);
	EXPECT_EQ(std::filesystem::file_size(path), kSectorBytes + 100);
	std::filesystem::remove(path);
}

} // namespace

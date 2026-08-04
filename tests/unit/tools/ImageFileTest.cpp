// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ImageFile.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <ostream>
#include <span>
#include <sstream>
#include <vector>

#include "revenant/core/Error.hpp"
#include "support/FailingStream.hpp"

namespace {

using revenant::imagegen::writeBytesTo;
using revenant::imagegen::writeImageBytes;
using revenant::testing::FailingBuf;

// Every imagegen writer's failure reporting funnels through this file, so this
// is where the funnel is asked what it says when writing goes wrong.

TEST(ImageFile, ReportsWhatAHealthyStreamTook) {
	std::ostringstream stream;
	const std::vector<std::byte> bytes(64, std::byte{0x5A});
	EXPECT_EQ(writeBytesTo(stream, bytes), 64U);
}

// An output iterator keeps going after the stream goes bad, so a count that was
// assumed rather than measured would become the caller's error offset.
TEST(ImageFile, ReportsOnlyWhatAFailingStreamTook) {
	FailingBuf buf{10};
	std::ostream stream{&buf};
	const std::vector<std::byte> bytes(64, std::byte{0x5A});
	EXPECT_EQ(writeBytesTo(stream, bytes), 10U);
}

TEST(ImageFile, ReportsNothingWhenTheStreamRefusesEverything) {
	FailingBuf buf{0};
	std::ostream stream{&buf};
	const std::vector<std::byte> bytes(8, std::byte{0x5A});
	EXPECT_EQ(writeBytesTo(stream, bytes), 0U);
}

// `Error::offset` is meaningful or absent. "We wrote all of it" is neither, and
// is what this returned before the writers counted what the stream took.
TEST(ImageFile, AnUnopenablePathFailsWithNoOffset) {
	const auto path = std::filesystem::temp_directory_path() / "no-such-dir" / "image.bin";
	const std::vector<std::byte> image(4096, std::byte{0x11});
	const auto written = writeImageBytes(path, image);
	ASSERT_FALSE(written.hasValue());
	EXPECT_EQ(written.error().code, revenant::ErrorCode::kIoFailure);
	EXPECT_EQ(written.error().offset, 0U);
}

TEST(ImageFile, AWritableImageReportsEveryByte) {
	const auto path = std::filesystem::temp_directory_path() / "revenant-imagefile.bin";
	const std::vector<std::byte> image(4096, std::byte{0x11});
	EXPECT_EQ(writeImageBytes(path, image).value(), 4096U);
	EXPECT_EQ(std::filesystem::file_size(path), 4096U);
	std::filesystem::remove(path);
}

} // namespace

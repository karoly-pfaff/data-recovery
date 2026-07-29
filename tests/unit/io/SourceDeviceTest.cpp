// SPDX-License-Identifier: GPL-3.0-or-later
// story-0040: the one place a source path becomes a device. What is asserted
// here is the *choice* — a regular file opens as an image, and anything else is
// handed to the raw-device branch. Opening an actual disk is not something a CI
// runner can be asked to do, so the branch is proven by where it sends the
// failure rather than by a device that is not there.
#include "revenant/core/io/SourceDevice.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

#include "support/TempDir.hpp"
#include "support/TempFile.hpp"

namespace {

using revenant::openSource;
using revenant::testing::TempDir;
using revenant::testing::TempFile;

[[nodiscard]] std::vector<std::byte> countingBytes(std::size_t count) {
	std::vector<std::byte> bytes(count);
	for (std::size_t at = 0; at < count; ++at) {
		bytes.at(at) = static_cast<std::byte>(static_cast<unsigned char>((at % 251) + 1));
	}
	return bytes;
}

TEST(SourceDevice, OpensARegularFileAsAnImage) {
	const auto content = countingBytes(2048);
	const TempFile image{content};
	auto device = openSource(image.path());
	ASSERT_TRUE(device.hasValue());
	EXPECT_EQ(device.value()->sizeInBytes(), 2048U);
}

TEST(SourceDevice, ReadsTheImagesOwnBytesBack) {
	const auto content = countingBytes(1024);
	const TempFile image{content};
	auto device = openSource(image.path());
	ASSERT_TRUE(device.hasValue());
	std::vector<std::byte> buffer(100);
	ASSERT_TRUE(device.value()->readAt(500, buffer).hasValue());
	EXPECT_TRUE(std::ranges::equal(buffer, std::span{content}.subspan(500, 100)));
}

// Not a file, so it goes to the device branch — where the OS refuses it. Which
// refusal it is differs by platform, and neither is worth pinning: what matters
// is that a directory never comes back as something a run would read.
TEST(SourceDevice, RefusesADirectory) {
	const TempDir directory;
	EXPECT_FALSE(openSource(directory.path()).hasValue());
}

TEST(SourceDevice, RefusesAPathThatNamesNothing) {
	EXPECT_FALSE(openSource("no-such-source.img").hasValue());
}

} // namespace

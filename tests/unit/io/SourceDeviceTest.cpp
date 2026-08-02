// SPDX-License-Identifier: GPL-3.0-or-later
// story-0401: the one place a source path becomes a device. What is asserted
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

#include "revenant/core/Error.hpp"
#include "support/TempDir.hpp"
#include "support/TempFile.hpp"

namespace {

using revenant::classifySource;
using revenant::openSource;
using revenant::SourceKind;
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
	EXPECT_EQ(device.value().top().sizeInBytes(), 2048U);
}

TEST(SourceDevice, ReadsTheImagesOwnBytesBack) {
	const auto content = countingBytes(1024);
	const TempFile image{content};
	auto device = openSource(image.path());
	ASSERT_TRUE(device.hasValue());
	std::vector<std::byte> buffer(100);
	ASSERT_TRUE(device.value().top().readAt(500, buffer).hasValue());
	EXPECT_TRUE(std::ranges::equal(buffer, std::span{content}.subspan(500, 100)));
}

// A share root, a mounted NFS or SMB path and a plain folder are all directories,
// and all expose only live files (ADR-0007, story-0406). Refused here rather
// than handed to the device layer, so the operator gets a sentence they can act
// on instead of the OS's complaint about opening a directory as a disk.
TEST(SourceDevice, RefusesADirectoryAsNotBlockAddressable) {
	const TempDir directory;
	const auto opened = openSource(directory.path());
	ASSERT_FALSE(opened.hasValue());
	EXPECT_EQ(opened.error().code, revenant::ErrorCode::kNotBlockAddressable);
}

TEST(SourceDevice, RefusesAPathThatNamesNothing) {
	EXPECT_FALSE(openSource("no-such-source.img").hasValue());
}

// The choice above, exposed. ADR-0005's destination rule applies a different
// test to an image than to a device, and it must reach that verdict the same
// way the open does rather than by asking the filesystem its own question
// (story-0609).
TEST(SourceDevice, ClassifiesARegularFileAsAnImage) {
	const TempFile image{countingBytes(512)};
	EXPECT_EQ(classifySource(image.path()), SourceKind::kImageFile);
}

TEST(SourceDevice, ClassifiesADirectoryAsNotBlockAddressable) {
	const TempDir directory;
	EXPECT_EQ(classifySource(directory.path()), SourceKind::kNotBlockAddressable);
}

TEST(SourceDevice, ClassifiesWhatIsNeitherFileNorFolderAsADevice) {
	EXPECT_EQ(classifySource("no-such-source.img"), SourceKind::kDevice);
}

} // namespace

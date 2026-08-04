// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ImageFile.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "support/FailingBuf.hpp"

namespace {

using revenant::imagegen::closeImage;
using revenant::imagegen::writeBytesTo;
using revenant::imagegen::writeImageBytes;
using revenant::imagegen::writeImageFile;
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

// The three states `closeImage` tells apart, driven directly — the only place
// a non-zero `Error::offset` out of this file is observed. Without it, changing
// `.offset = filled.written` to a constant would leave the suite green, and the
// count `writeBytesTo` measures would never be shown to reach the error.
TEST(ImageFile, AFillThatStoppedShortReportsWhereItStopped) {
	std::ofstream stream{std::filesystem::temp_directory_path() / "revenant-outcome.bin"};
	const auto outcome = closeImage(stream, {.written = 1234, .complete = false});
	ASSERT_FALSE(outcome.hasValue());
	EXPECT_EQ(outcome.error().code, revenant::ErrorCode::kIoFailure);
	EXPECT_EQ(outcome.error().offset, 1234U);
	std::filesystem::remove(std::filesystem::temp_directory_path() / "revenant-outcome.bin");
}

TEST(ImageFile, AFillThatFinishedOnAGoodStreamReportsItsCount) {
	const auto path = std::filesystem::temp_directory_path() / "revenant-outcome-ok.bin";
	std::ofstream stream{path};
	EXPECT_EQ(closeImage(stream, {.written = 99, .complete = true}).value(), 99U);
	std::filesystem::remove(path);
}

// What `/dev/full` made of a small image, and whether the kernel took the write
// before refusing the close. Split from the assertions below so the test body
// stays inside the complexity limit.
struct FullDiskAttempt {
	revenant::Result<std::uint64_t> written = revenant::Error{};
	bool filledCleanly = false;
};

[[nodiscard]] FullDiskAttempt writeToDevFull() {
	FullDiskAttempt attempt;
	attempt.written = writeImageFile("/dev/full", [&attempt](std::ostream& stream) {
		const std::vector<std::byte> image(64, std::byte{0x11});
		const auto count = writeBytesTo(stream, image);
		attempt.filledCleanly = stream.good();
		return count;
	});
	return attempt;
}

// Why the attempt should be rejected, or empty when it was. One value rather
// than five assertions, so the test body says one thing — and so a failure
// names which part of the contract broke.
[[nodiscard]] std::string verdictOf(const FullDiskAttempt& attempt) {
	if (!attempt.filledCleanly) {
		return "/dev/full refused the write itself; this says nothing about the close";
	}
	if (attempt.written.hasValue()) {
		return "an image that never reached the filesystem was reported as written";
	}
	const auto failure = attempt.written.error();
	const bool typed = failure.code == revenant::ErrorCode::kIoFailure && failure.offset == 0;
	return typed ? "" : "not a typed I/O failure, or a failed flush claimed an offset";
}

// A full disk as the operating system actually produces one. `/dev/full` takes
// the write into the stream's buffer and refuses it at the close, which is what
// `closeImage`'s close exists for: before it, this returned the byte count and
// success for an image that never reached a filesystem.
//
// The test proves its own premise. Both the open-failure and the close-failure
// paths end in `kIoFailure` with no offset, so asserting the result alone would
// pass on a host with no `/dev/full` — through the wrong branch, and with the
// close reverted. `filledCleanly` is what separates them: true only if the
// kernel really took the write.
//
// Linux and the BSDs have this device; macOS does not, and POSIX does not
// specify it — hence a runtime check and a skip rather than a platform guard.
TEST(ImageFile, AFullDiskIsNotReportedAsAWrittenImage) {
	if (!std::filesystem::exists("/dev/full")) {
		GTEST_SKIP() << "no /dev/full here; the close-failure path is unexercised";
	}
	EXPECT_EQ(verdictOf(writeToDevFull()), "");
}

TEST(ImageFile, AWritableImageReportsEveryByte) {
	const auto path = std::filesystem::temp_directory_path() / "revenant-imagefile.bin";
	const std::vector<std::byte> image(4096, std::byte{0x11});
	EXPECT_EQ(writeImageBytes(path, image).value(), 4096U);
	EXPECT_EQ(std::filesystem::file_size(path), 4096U);
	std::filesystem::remove(path);
}

} // namespace

// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/OutputPath.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>

#include "revenant/core/Error.hpp"

namespace {

using revenant::ErrorCode;
using revenant::recovery::kMaxSegmentBytes;
using revenant::recovery::kMaxSegments;
using revenant::recovery::sanitizeOutputPath;

const std::filesystem::path& testRoot() {
	static const std::filesystem::path kRoot =
		std::filesystem::temp_directory_path() / "revenant-output-path-test-root";
	return kRoot;
}

TEST(OutputPath, TraversalIsRejected) {
	const auto result = sanitizeOutputPath(testRoot(), "../../etc/passwd");
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(OutputPath, UnixAbsolutePathIsRejected) {
	const auto result = sanitizeOutputPath(testRoot(), "/etc/x");
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(OutputPath, DrivePrefixedPathIsRejected) {
	const auto result = sanitizeOutputPath(testRoot(), "C:\\evil");
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(OutputPath, UncSharePathIsRejected) {
	const auto result = sanitizeOutputPath(testRoot(), R"(\\server\share)");
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(OutputPath, EmbeddedNulIsRejected) {
	std::string name = "bad";
	name.push_back('\0');
	name += "name.jpg";
	const auto result = sanitizeOutputPath(testRoot(), name);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(OutputPath, ControlByteIsRejected) {
	std::string name = "bad";
	name.push_back(static_cast<char>(0x01));
	name += "name.jpg";
	const auto result = sanitizeOutputPath(testRoot(), name);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(OutputPath, ReservedNameWithExtensionIsNeutralized) {
	const auto result = sanitizeOutputPath(testRoot(), "CON.jpg");
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().string().starts_with(testRoot().string()));
	EXPECT_EQ(result.value().filename().string(), "_CON.jpg");
}

TEST(OutputPath, ReservedNameLowercaseIsNeutralized) {
	const auto result = sanitizeOutputPath(testRoot(), "con");
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().string().starts_with(testRoot().string()));
	EXPECT_EQ(result.value().filename().string(), "_con");
}

TEST(OutputPath, ReservedComPortNameIsNeutralizedCaseInsensitively) {
	const auto result = sanitizeOutputPath(testRoot(), "com1.txt");
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().string().starts_with(testRoot().string()));
	EXPECT_EQ(result.value().filename().string(), "_com1.txt");
}

TEST(OutputPath, ReservedNameWithTrailingSpaceAndNoDotIsNeutralized) {
	const auto result = sanitizeOutputPath(testRoot(), "CON ");
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().string().starts_with(testRoot().string()));
	EXPECT_EQ(result.value().filename().string(), "_CON");
}

TEST(OutputPath, ReservedNameWithMultipleTrailingSpacesIsNeutralized) {
	const auto result = sanitizeOutputPath(testRoot(), "NUL  ");
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().string().starts_with(testRoot().string()));
	EXPECT_EQ(result.value().filename().string(), "_NUL");
}

TEST(OutputPath, ReservedComPortNameWithTrailingSpaceIsNeutralized) {
	const auto result = sanitizeOutputPath(testRoot(), "COM1 ");
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().string().starts_with(testRoot().string()));
	EXPECT_EQ(result.value().filename().string(), "_COM1");
}

TEST(OutputPath, ReservedNameSurvivesStrippingThenNeutralization) {
	const auto result = sanitizeOutputPath(testRoot(), "CON . .");
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().string().starts_with(testRoot().string()));
	EXPECT_EQ(result.value().filename().string(), "_CON");
}

TEST(OutputPath, NonReservedNameWithReservedPrefixPasses) {
	const auto result = sanitizeOutputPath(testRoot(), "CONX");
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().string().starts_with(testRoot().string()));
	EXPECT_EQ(result.value().filename().string(), "CONX");
}

TEST(OutputPath, TrailingDotIsStripped) {
	const auto result = sanitizeOutputPath(testRoot(), "name.");
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().string().starts_with(testRoot().string()));
	EXPECT_EQ(result.value().filename().string(), "name");
}

TEST(OutputPath, TrailingSpaceIsStripped) {
	const auto result = sanitizeOutputPath(testRoot(), "name ");
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().string().starts_with(testRoot().string()));
	EXPECT_EQ(result.value().filename().string(), "name");
}

TEST(OutputPath, NestedPathIsPreservedUnderRoot) {
	const auto result = sanitizeOutputPath(testRoot(), "a/b/c.jpg");
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().string().starts_with(testRoot().string()));
	EXPECT_EQ(result.value(), (testRoot() / "a" / "b" / "c.jpg").lexically_normal());
}

TEST(OutputPath, DotAndEmptySegmentsAreDropped) {
	const auto result = sanitizeOutputPath(testRoot(), "./a/./b.jpg");
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().string().starts_with(testRoot().string()));
	EXPECT_EQ(result.value(), (testRoot() / "a" / "b.jpg").lexically_normal());
}

TEST(OutputPath, DoubleSeparatorCollapsesEmptySegment) {
	const auto result = sanitizeOutputPath(testRoot(), "a//b.jpg");
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().string().starts_with(testRoot().string()));
	EXPECT_EQ(result.value(), (testRoot() / "a" / "b.jpg").lexically_normal());
}

TEST(OutputPath, AllSegmentsDroppedIsInvalidArgument) {
	const auto result = sanitizeOutputPath(testRoot(), ".");
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(OutputPath, EmptyNameIsInvalidArgument) {
	const auto result = sanitizeOutputPath(testRoot(), "");
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(OutputPath, SegmentAtMaxLengthIsAccepted) {
	const std::string longSegment(kMaxSegmentBytes, 'a');
	const auto result = sanitizeOutputPath(testRoot(), longSegment);
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().string().starts_with(testRoot().string()));
}

TEST(OutputPath, SegmentOverMaxLengthIsRejected) {
	const std::string tooLong(kMaxSegmentBytes + 1, 'a');
	const auto result = sanitizeOutputPath(testRoot(), tooLong);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(OutputPath, SegmentCountAtBoundIsAccepted) {
	std::string name;
	for (std::size_t i = 0; i + 1 < kMaxSegments; ++i) {
		name += "d/";
	}
	name += "file.jpg";
	const auto result = sanitizeOutputPath(testRoot(), name);
	ASSERT_TRUE(result.hasValue());
	EXPECT_TRUE(result.value().string().starts_with(testRoot().string()));
}

TEST(OutputPath, SegmentCountOverBoundIsRejected) {
	std::string name;
	for (std::size_t i = 0; i < kMaxSegments; ++i) {
		name += "d/";
	}
	name += "file.jpg";
	const auto result = sanitizeOutputPath(testRoot(), name);
	ASSERT_FALSE(result.hasValue());
	EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

// A real directory reached through a symlink, or empty where the platform does
// not permit one (Windows outside developer mode).
std::filesystem::path makeAliasedRoot() {
	const auto base = testRoot() / "alias-case";
	std::error_code ec;
	std::filesystem::create_directories(base / "real", ec);
	const auto link = base / "link";
	std::filesystem::remove(link, ec);
	std::filesystem::create_directory_symlink(base / "real", link, ec);
	return ec ? std::filesystem::path{} : link;
}

// Containment used to compare the assembled path, which keeps the caller's
// spelling of the root, against the *canonical* root — two different namings of
// the same directory whenever a filesystem alias sits in between, which
// rejected every legitimate name. A symlinked root reproduces it portably; on
// Windows the same mismatch arrives as an 8.3 short name, which is why CI's
// temp directory ("RUNNER~1") failed every positive case while long-form local
// paths passed. The result must keep the caller's spelling of the root.
TEST(OutputPath, RootReachedThroughAnAliasIsAccepted) {
	const auto link = makeAliasedRoot();
	if (link.empty()) {
		GTEST_SKIP() << "directory symlinks unavailable on this platform";
	}
	const auto result = sanitizeOutputPath(link, "photo.jpg");
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value(), link / "photo.jpg");
}

} // namespace

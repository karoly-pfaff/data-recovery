// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ntfs/AttributeBuilder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <vector>

#include "fs/ntfs/MftAttributes.hpp"
#include "revenant/fs/ntfs/Runlist.hpp"

namespace {

using revenant::fs::ntfs::AttributeView;
using revenant::fs::ntfs::DataRun;
using revenant::fs::ntfs::decodeRunlist;
using revenant::fs::ntfs::parseDataAttribute;
using revenant::fs::ntfs::parseFileName;
using revenant::fs::ntfs::parseStandardInformation;
using revenant::fs::ntfs::readAttributeView;
using revenant::imagegen::ntfs::buildFileName;
using revenant::imagegen::ntfs::buildNonResidentData;
using revenant::imagegen::ntfs::buildResidentData;
using revenant::imagegen::ntfs::buildStandardInformation;
using revenant::imagegen::ntfs::FileNameSpec;
using revenant::imagegen::ntfs::kFixtureAccessed;
using revenant::imagegen::ntfs::kFixtureCreated;
using revenant::imagegen::ntfs::kFixtureModified;
using revenant::imagegen::ntfs::NonResidentDataSpec;

// Every builder below is specified against the production attribute readers:
// the header it emits must parse, and its content must read back unchanged.
[[nodiscard]] AttributeView parseHeader(const std::vector<std::byte>& attribute) {
	const auto view = readAttributeView(attribute, 0);
	EXPECT_TRUE(view.hasValue());
	return view.hasValue() ? view.value() : AttributeView{};
}

[[nodiscard]] std::span<const std::byte>
residentContent(const std::vector<std::byte>& attribute, const AttributeView& view) {
	return std::span{attribute}.subspan(view.contentOffset, view.contentLength);
}

[[nodiscard]] std::vector<std::byte> payload(std::size_t size) {
	std::vector<std::byte> out(size, std::byte{0});
	for (std::size_t i = 0; i < size; ++i) {
		out.at(i) = static_cast<std::byte>(i & 0xFFU);
	}
	return out;
}

TEST(AttributeBuilder, StandardInformationParsesBackWithTheFixtureTimestamps) {
	const auto attribute = buildStandardInformation();
	const auto view = parseHeader(attribute);
	EXPECT_EQ(view.type, 0x10U);
	EXPECT_FALSE(view.nonResident);
	const auto info = parseStandardInformation(residentContent(attribute, view));
	ASSERT_TRUE(info.hasValue());
	EXPECT_EQ(info.value().created, kFixtureCreated);
	EXPECT_EQ(info.value().modified, kFixtureModified);
	EXPECT_EQ(info.value().accessed, kFixtureAccessed);
}

TEST(AttributeBuilder, FileNameParsesBackWithNameParentAndSize) {
	const auto attribute = buildFileName(
		FileNameSpec{
			.parentRecord = 16,
			.parentSequence = 2,
			.name = "deleted.jpg",
			.realSize = 9000});
	const auto view = parseHeader(attribute);
	EXPECT_EQ(view.type, 0x30U);
	const auto name = parseFileName(residentContent(attribute, view));
	ASSERT_TRUE(name.hasValue());
	EXPECT_EQ(name.value().parentRecord, 16U);
	EXPECT_EQ(name.value().parentSequence, 2U);
	EXPECT_EQ(name.value().realSize, 9000U);
	EXPECT_EQ(name.value().name.utf8, "deleted.jpg");
	EXPECT_TRUE(name.value().name.lossless);
}

TEST(AttributeBuilder, ResidentDataCarriesItsContentVerbatim) {
	const auto content = payload(37);
	const auto attribute = buildResidentData(content);
	const auto view = parseHeader(attribute);
	EXPECT_EQ(view.type, 0x80U);
	EXPECT_FALSE(view.nonResident);
	const auto data = parseDataAttribute(view, attribute);
	ASSERT_TRUE(data.hasValue());
	ASSERT_TRUE(data.value().resident);
	EXPECT_EQ(data.value().residentContent, content);
}

TEST(AttributeBuilder, NonResidentDataCarriesADecodableRunlist) {
	const std::vector<DataRun> runs{
		DataRun{.startCluster = 40, .lengthClusters = 2, .sparse = false},
		DataRun{.startCluster = 70, .lengthClusters = 1, .sparse = false}};
	const auto attribute = buildNonResidentData(
		NonResidentDataSpec{.runs = runs, .realSize = 9000, .bytesPerCluster = 4096});
	const auto view = parseHeader(attribute);
	EXPECT_TRUE(view.nonResident);
	EXPECT_EQ(view.realSize, 9000U);
	const auto data = parseDataAttribute(view, attribute);
	ASSERT_TRUE(data.hasValue());
	ASSERT_FALSE(data.value().resident);
	const auto decoded = decodeRunlist(data.value().runlistBytes);
	ASSERT_TRUE(decoded.hasValue());
	ASSERT_EQ(decoded.value().runs.size(), 2U);
	EXPECT_EQ(decoded.value().runs.at(0).startCluster, 40U);
	EXPECT_EQ(decoded.value().runs.at(1).startCluster, 70U);
	EXPECT_EQ(decoded.value().totalClusters, 3U);
}

TEST(AttributeBuilder, EveryAttributeLengthIsEightByteAligned) {
	EXPECT_EQ(buildStandardInformation().size() % 8, 0U);
	EXPECT_EQ(buildResidentData(payload(13)).size() % 8, 0U);
	EXPECT_EQ(
		buildFileName(
			FileNameSpec{.parentRecord = 5, .parentSequence = 1, .name = "a.txt", .realSize = 1})
				.size() %
			8,
		0U);
}

} // namespace

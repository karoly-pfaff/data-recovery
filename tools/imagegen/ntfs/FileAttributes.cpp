// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "imagegen/ByteWriter.hpp"
#include "imagegen/ntfs/AttributeBuilder.hpp"
#include "imagegen/ntfs/AttributeInternal.hpp"

namespace revenant::imagegen::ntfs {

namespace {

constexpr std::size_t kStandardInformationBytes = 0x30;
constexpr std::size_t kFileNameHeaderBytes = 0x42;
constexpr std::uint64_t kSequenceShift = 48;
constexpr std::uint8_t kWin32NameSpace = 1;

// $STANDARD_INFORMATION field positions.
constexpr std::size_t kCreatedOffset = 0x00;
constexpr std::size_t kModifiedOffset = 0x08;
constexpr std::size_t kMftChangedOffset = 0x10;
constexpr std::size_t kAccessedOffset = 0x18;

// $FILE_NAME field positions.
constexpr std::size_t kParentReferenceOffset = 0x00;
constexpr std::size_t kNameCreatedOffset = 0x08;
constexpr std::size_t kNameModifiedOffset = 0x10;
constexpr std::size_t kNameAccessedOffset = 0x20;
constexpr std::size_t kAllocatedSizeOffset = 0x28;
constexpr std::size_t kRealSizeOffset = 0x30;
constexpr std::size_t kNameLengthOffset = 0x40;
constexpr std::size_t kNameSpaceOffset = 0x41;

// The parent is one 64-bit reference: record number in the low 48 bits,
// sequence number in the high 16.
[[nodiscard]] std::uint64_t parentReference(const FileNameSpec& spec) noexcept {
	return spec.parentRecord | (static_cast<std::uint64_t>(spec.parentSequence) << kSequenceShift);
}

void putNameTimestamps(std::vector<std::byte>& content) {
	putLe<std::uint64_t>(content, kNameCreatedOffset, kFixtureCreated);
	putLe<std::uint64_t>(content, kNameModifiedOffset, kFixtureModified);
	putLe<std::uint64_t>(content, kNameAccessedOffset, kFixtureAccessed);
}

[[nodiscard]] std::vector<std::byte> fileNameContent(const FileNameSpec& spec) {
	const auto name = widenAscii(spec.name);
	std::vector<std::byte> content(kFileNameHeaderBytes + name.size(), std::byte{0});
	putLe<std::uint64_t>(content, kParentReferenceOffset, parentReference(spec));
	putNameTimestamps(content);
	putLe<std::uint64_t>(content, kAllocatedSizeOffset, spec.realSize);
	putLe<std::uint64_t>(content, kRealSizeOffset, spec.realSize);
	putLe<std::uint8_t>(content, kNameLengthOffset, static_cast<std::uint8_t>(spec.name.size()));
	putLe<std::uint8_t>(content, kNameSpaceOffset, kWin32NameSpace);
	putBytes(content, kFileNameHeaderBytes, name);
	return content;
}

} // namespace

std::vector<std::byte> widenAscii(std::span<const char> ascii) {
	std::vector<std::byte> utf16;
	utf16.reserve(ascii.size() * 2);
	for (const char letter : ascii) {
		utf16.push_back(static_cast<std::byte>(letter));
		utf16.push_back(std::byte{0});
	}
	return utf16;
}

std::vector<std::byte> buildStandardInformation() {
	std::vector<std::byte> content(kStandardInformationBytes, std::byte{0});
	putLe<std::uint64_t>(content, kCreatedOffset, kFixtureCreated);
	putLe<std::uint64_t>(content, kModifiedOffset, kFixtureModified);
	putLe<std::uint64_t>(content, kMftChangedOffset, kFixtureModified);
	putLe<std::uint64_t>(content, kAccessedOffset, kFixtureAccessed);
	return residentAttribute(kStandardInformationType, content);
}

std::vector<std::byte> buildFileName(const FileNameSpec& spec) {
	return residentAttribute(kFileNameType, fileNameContent(spec));
}

} // namespace revenant::imagegen::ntfs

// SPDX-License-Identifier: GPL-3.0-or-later
#include "MftRecordTestSupport.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "revenant/core/Endian.hpp"

namespace {

using revenant::toLittleEndian;

void writeLeAt(std::vector<std::byte>& record, std::uint64_t offset, auto value) {
	const auto bytes = toLittleEndian<decltype(value)>(value);
	const std::span<std::byte> target{record};
	[[maybe_unused]] const auto _ = std::ranges::copy(
		bytes,
		target.subspan(static_cast<std::size_t>(offset), bytes.size()).begin());
}

void writeBytesAt(
	std::vector<std::byte>& record,
	std::uint64_t offset,
	std::span<const std::byte> bytes) {
	const std::span<std::byte> target{record};
	[[maybe_unused]] const auto _ = std::ranges::copy(
		bytes,
		target.subspan(static_cast<std::size_t>(offset), bytes.size()).begin());
}

[[nodiscard]] std::vector<std::byte> utf16Le(std::string_view name) {
	std::vector<std::byte> out;
	out.reserve(name.size() * 2);
	for (const char ch : name) {
		out.push_back(static_cast<std::byte>(ch));
		out.push_back(std::byte{0});
	}
	return out;
}

void writeRecordHeader(std::vector<std::byte>& record) {
	const std::array<std::byte, 4> file{
		std::byte{'F'},
		std::byte{'I'},
		std::byte{'L'},
		std::byte{'E'}};
	writeBytesAt(record, 0x00, file);
	writeLeAt(record, 0x04, static_cast<std::uint16_t>(0x30));
	writeLeAt(record, 0x06, static_cast<std::uint16_t>(3));
	writeLeAt(record, 0x10, static_cast<std::uint16_t>(1));
	writeLeAt(record, 0x14, static_cast<std::uint16_t>(0x38));
	writeLeAt(record, 0x16, static_cast<std::uint16_t>(0x01));
	writeLeAt(record, 0x18, static_cast<std::uint32_t>(0x118));
	writeLeAt(record, 0x20, static_cast<std::uint64_t>(0ULL));
}

void writeUpdateSequence(std::vector<std::byte>& record) {
	constexpr std::uint16_t kUsn = 0x1234;
	writeLeAt(record, 0x30, kUsn);
	writeLeAt(record, 0x32, static_cast<std::uint16_t>(0xABCD));
	writeLeAt(record, 0x34, static_cast<std::uint16_t>(0xEF01));
	writeLeAt(record, 0x1FE, kUsn);
	writeLeAt(record, 0x3FE, kUsn);
}

void writeStandardInfoContent(std::vector<std::byte>& record, std::uint64_t offset) {
	writeLeAt(record, offset, static_cast<std::uint64_t>(0x1111ULL));
	writeLeAt(record, offset + 0x08, static_cast<std::uint64_t>(0x2222ULL));
	writeLeAt(record, offset + 0x10, static_cast<std::uint64_t>(0x3333ULL));
	writeLeAt(record, offset + 0x18, static_cast<std::uint64_t>(0x4444ULL));
}

void writeStandardInfoAttribute(std::vector<std::byte>& record, std::uint64_t offset) {
	writeLeAt(record, offset, static_cast<std::uint32_t>(0x10));
	writeLeAt(record, offset + 0x04, static_cast<std::uint32_t>(0x48));
	writeLeAt(record, offset + 0x08, static_cast<std::uint8_t>(0));
	writeLeAt(record, offset + 0x09, static_cast<std::uint8_t>(0));
	writeLeAt(record, offset + 0x0A, static_cast<std::uint16_t>(0x40));
	writeLeAt(record, offset + 0x10, static_cast<std::uint32_t>(0x30));
	writeLeAt(record, offset + 0x14, static_cast<std::uint16_t>(0x18));
	writeStandardInfoContent(record, offset + 0x18);
}

void writeFileNameTimestamps(std::vector<std::byte>& record, std::uint64_t offset) {
	writeLeAt(record, offset + 0x08, static_cast<std::uint64_t>(0x5555ULL));
	writeLeAt(record, offset + 0x10, static_cast<std::uint64_t>(0x6666ULL));
	writeLeAt(record, offset + 0x20, static_cast<std::uint64_t>(0x7777ULL));
}

void writeFileNameContent(
	std::vector<std::byte>& record,
	std::uint64_t offset,
	const std::vector<std::byte>& nameBytes) {
	writeLeAt(record, offset, static_cast<std::uint64_t>(5ULL | (1ULL << 48)));
	writeFileNameTimestamps(record, offset);
	writeLeAt(record, offset + 0x30, static_cast<std::uint64_t>(0ULL));
	writeLeAt(record, offset + 0x38, static_cast<std::uint32_t>(0));
	writeLeAt(record, offset + 0x40, static_cast<std::uint8_t>(nameBytes.size() / 2));
	writeLeAt(record, offset + 0x41, static_cast<std::uint8_t>(1));
	writeBytesAt(record, offset + 0x42, nameBytes);
}

void writeFileNameAttributeHeader(
	std::vector<std::byte>& record,
	std::uint64_t offset,
	std::uint32_t length,
	std::uint32_t contentLength) {
	writeLeAt(record, offset, static_cast<std::uint32_t>(0x30));
	writeLeAt(record, offset + 0x04, length);
	writeLeAt(record, offset + 0x08, static_cast<std::uint8_t>(0));
	writeLeAt(record, offset + 0x09, static_cast<std::uint8_t>(0));
	writeLeAt(record, offset + 0x0A, static_cast<std::uint16_t>(0x40));
	writeLeAt(record, offset + 0x10, contentLength);
	writeLeAt(record, offset + 0x14, static_cast<std::uint16_t>(0x18));
}

void writeFileNameAttribute(std::vector<std::byte>& record, std::uint64_t offset) {
	const auto nameBytes = utf16Le("photo.jpg");
	const auto contentLength = static_cast<std::uint32_t>(0x42 + nameBytes.size());
	const auto length = ((0x18 + contentLength + 7) / 8) * 8;
	writeFileNameAttributeHeader(record, offset, length, contentLength);
	writeFileNameContent(record, offset + 0x18, nameBytes);
}

void writeDataAttributeHeader(
	std::vector<std::byte>& record,
	std::uint64_t offset,
	std::uint32_t length,
	std::uint32_t contentLength) {
	writeLeAt(record, offset, static_cast<std::uint32_t>(0x80));
	writeLeAt(record, offset + 0x04, length);
	writeLeAt(record, offset + 0x08, static_cast<std::uint8_t>(0));
	writeLeAt(record, offset + 0x09, static_cast<std::uint8_t>(0));
	writeLeAt(record, offset + 0x0A, static_cast<std::uint16_t>(0x40));
	writeLeAt(record, offset + 0x10, contentLength);
	writeLeAt(record, offset + 0x14, static_cast<std::uint16_t>(0x18));
}

void writeDataPayload(
	std::vector<std::byte>& record,
	std::uint64_t offset,
	std::string_view payload) {
	for (std::size_t i = 0; i < payload.size(); ++i) {
		record.at(offset + i) = static_cast<std::byte>(payload.at(i));
	}
}

void writeDataAttribute(std::vector<std::byte>& record, std::uint64_t offset) {
	const std::string payload = "hello-ntfs";
	const auto contentLength = static_cast<std::uint32_t>(payload.size());
	const auto length = ((0x18 + contentLength + 7) / 8) * 8;
	writeDataAttributeHeader(record, offset, length, contentLength);
	writeDataPayload(record, offset + 0x18, payload);
}

} // namespace

namespace mft_record_test_support {

std::vector<std::byte> makeValidRecord() {
	std::vector<std::byte> record(kRecordSize);
	writeRecordHeader(record);
	writeUpdateSequence(record);
	writeStandardInfoAttribute(record, kStandardInfoAttributeOffset);
	writeFileNameAttribute(record, kFileNameAttributeOffset);
	writeDataAttribute(record, kDataAttributeOffset);
	return record;
}

} // namespace mft_record_test_support

// SPDX-License-Identifier: GPL-3.0-or-later
#include "MftAttributes.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/NameDecode.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ntfs/MftRecord.hpp"

namespace revenant::fs::ntfs {

namespace {

constexpr std::uint32_t kAttributeEnd = 0xFFFFFFFFU;
constexpr std::size_t kAttributeHeaderSize = 0x10;
constexpr std::size_t kMinAttributeLength = 0x18;
constexpr std::size_t kNonResidentHeaderSize = 0x40;
constexpr std::uint64_t kRecordMask = 0x0000FFFFFFFFFFFFULL;
constexpr std::uint64_t kSequenceShift = 48ULL;

[[nodiscard]] bool
attributeLengthValid(std::uint32_t length, std::uint64_t offset, std::size_t recordSize) {
	return length >= kMinAttributeLength && (length & 0x07U) == 0 && offset + length <= recordSize;
}

[[nodiscard]] Result<AttributeView>
readResidentTail(AttributeView view, std::span<const std::byte> record) {
	const ByteReader reader{record};
	view.contentLength = reader.readLe<std::uint32_t>(view.offset + 0x10).value();
	view.contentOffset = reader.readLe<std::uint16_t>(view.offset + 0x14).value();
	// Widened before summing: in the attribute's own 32-bit width a hostile
	// content length wraps and slips past the range it should fail.
	const auto contentEnd = static_cast<std::uint64_t>(view.contentOffset) +
							static_cast<std::uint64_t>(view.contentLength);
	if (view.contentOffset < kMinAttributeLength || contentEnd > view.length) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = view.offset + 0x10};
	}
	return view;
}

[[nodiscard]] Result<AttributeView>
readNonResidentTail(AttributeView view, std::span<const std::byte> record) {
	if (view.length < kNonResidentHeaderSize) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = view.offset + 4};
	}
	const ByteReader reader{record};
	view.runlistOffset = reader.readLe<std::uint16_t>(view.offset + 0x20).value();
	view.realSize = reader.readLe<std::uint64_t>(view.offset + 0x30).value();
	if (view.runlistOffset < kNonResidentHeaderSize || view.runlistOffset > view.length) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = view.offset + 0x20};
	}
	return view;
}

// Reads the fixed 16-byte header that follows a real attribute type code.
// Carving the header out as its own span makes every field read provably
// in-range, so the reads below cannot fail.
[[nodiscard]] Result<AttributeView>
readAttributeHeader(const ByteReader& reader, std::uint64_t offset, std::uint32_t type) {
	const auto header = reader.bytes(offset, kAttributeHeaderSize);
	if (!header.hasValue()) {
		return header.error();
	}
	const ByteReader head{header.value()};
	const auto length = head.readLe<std::uint32_t>(4).value();
	if (!attributeLengthValid(length, offset, reader.size())) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = offset + 4};
	}
	return AttributeView{
		.type = type,
		.length = length,
		.offset = offset,
		.nonResident = head.readLe<std::uint8_t>(8).value() != 0,
		.nameLength = head.readLe<std::uint8_t>(9).value(),
		.nameOffset = head.readLe<std::uint16_t>(10).value()};
}

// The 4-byte type code is the only field an attribute is guaranteed to carry:
// the end marker is a bare type with no header behind it. A type code that runs
// off the end of the record is a typed kOutOfRange, not a discarded read.
[[nodiscard]] Result<AttributeView>
readAttributeBase(std::span<const std::byte> record, std::uint64_t offset) {
	const ByteReader reader{record};
	return reader.readLe<std::uint32_t>(offset).andThen([&reader, offset](std::uint32_t type) {
		if (type == kAttributeEnd) {
			return Result<AttributeView>(AttributeView{.type = kAttributeEnd});
		}
		return readAttributeHeader(reader, offset, type);
	});
}

[[nodiscard]] Result<MftFileName> readFileNameText(
	std::span<const std::byte> content,
	MftFileName fileName,
	std::size_t nameLengthChars) {
	const auto nameBytes = nameLengthChars * 2;
	const auto maxNameBytes = content.size() - 0x42;
	if (nameBytes > maxNameBytes) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = 0x40};
	}
	const ByteReader reader{content};
	const auto rawName = reader.bytes(0x42, nameBytes).value();
	fileName.name = decodeUtf16Name(rawName);
	return fileName;
}

[[nodiscard]] Result<MftFileName>
readFileNameHeader(std::span<const std::byte> content, std::size_t& nameLengthChars) {
	MftFileName fileName;
	const ByteReader reader{content};
	const auto parentRef = reader.readLe<std::uint64_t>(0x00).value();
	fileName.parentRecord = parentRef & kRecordMask;
	fileName.parentSequence = static_cast<std::uint16_t>(parentRef >> kSequenceShift);
	fileName.realSize = reader.readLe<std::uint64_t>(0x30).value();
	fileName.nameSpace = reader.readLe<std::uint8_t>(0x41).value();
	nameLengthChars = static_cast<std::size_t>(reader.readLe<std::uint8_t>(0x40).value());
	return fileName;
}

[[nodiscard]] Result<MftData>
readResidentData(const AttributeView& view, std::span<const std::byte> record) {
	MftData data;
	const auto contentStart = view.offset + view.contentOffset;
	if (contentStart + view.contentLength > record.size()) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = contentStart};
	}
	data.resident = true;
	const auto content = record.subspan(static_cast<std::size_t>(contentStart), view.contentLength);
	data.residentContent.assign(content.begin(), content.end());
	data.realSize = view.contentLength;
	return data;
}

[[nodiscard]] Result<MftData>
readNonResidentData(const AttributeView& view, std::span<const std::byte> record) {
	MftData data;
	const auto runlistStart = view.offset + view.runlistOffset;
	const auto runlistLength = view.length - view.runlistOffset;
	if (runlistStart + runlistLength > record.size()) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = runlistStart};
	}
	data.resident = false;
	data.runlistBytes = record.subspan(
		static_cast<std::size_t>(runlistStart),
		static_cast<std::size_t>(runlistLength));
	data.realSize = view.realSize;
	return data;
}

} // namespace

Result<AttributeView> readAttributeView(std::span<const std::byte> record, std::uint64_t offset) {
	const auto base = readAttributeBase(record, offset);
	if (!base.hasValue()) {
		return base.error();
	}
	const auto& view = base.value();
	if (view.type == kAttributeEnd) {
		return {view};
	}
	return view.nonResident ? readNonResidentTail(view, record) : readResidentTail(view, record);
}

Result<Timestamps> parseStandardInformation(std::span<const std::byte> content) {
	if (content.size() < 0x20) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = content.size()};
	}
	const ByteReader reader{content};
	return Timestamps{
		.created = reader.readLe<std::uint64_t>(0x00).value(),
		.modified = reader.readLe<std::uint64_t>(0x08).value(),
		.accessed = reader.readLe<std::uint64_t>(0x18).value()};
}

Result<MftFileName> parseFileName(std::span<const std::byte> content) {
	if (content.size() < 0x42) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = content.size()};
	}
	std::size_t nameLengthChars = 0;
	const auto fileName = readFileNameHeader(content, nameLengthChars);
	if (!fileName.hasValue()) {
		return fileName.error();
	}
	return readFileNameText(content, fileName.value(), nameLengthChars);
}

Result<MftData> parseDataAttribute(const AttributeView& view, std::span<const std::byte> record) {
	if (view.nonResident) {
		return readNonResidentData(view, record);
	}
	return readResidentData(view, record);
}

} // namespace revenant::fs::ntfs

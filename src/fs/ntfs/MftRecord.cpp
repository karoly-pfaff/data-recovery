// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/ntfs/MftRecord.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "MftRecordInternal.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::ntfs {

namespace {

constexpr std::size_t kRecordHeaderSize = 0x38;
constexpr std::uint16_t kInUseFlag = 0x01;
constexpr std::uint16_t kDirectoryFlag = 0x02;

[[nodiscard]] bool signatureIsFile(std::span<const std::byte> raw) {
	if (raw.size() < 4) {
		return false;
	}
	const std::array<std::byte, 4> signature{
		std::byte{'F'},
		std::byte{'I'},
		std::byte{'L'},
		std::byte{'E'}};
	return std::ranges::equal(raw.first(4), signature);
}

[[nodiscard]] bool firstAttributeOffsetValid(
	std::uint16_t firstAttributeOffset,
	std::uint32_t usedSize,
	std::size_t recordSize) {
	return firstAttributeOffset >= kRecordHeaderSize && firstAttributeOffset <= usedSize &&
		   usedSize <= recordSize;
}

[[nodiscard]] Result<RecordHeader> readRecordHeaderFields(std::span<const std::byte> raw) {
	const ByteReader reader{raw};
	const auto h = RecordHeader{
		.usaOffset = reader.readLe<std::uint16_t>(0x04).value(),
		.usaCount = reader.readLe<std::uint16_t>(0x06).value(),
		.sequence = reader.readLe<std::uint16_t>(0x10).value(),
		.firstAttributeOffset = reader.readLe<std::uint16_t>(0x14).value(),
		.flags = reader.readLe<std::uint16_t>(0x16).value(),
		.usedSize = reader.readLe<std::uint32_t>(0x18).value(),
		.baseRecord = reader.readLe<std::uint64_t>(0x20).value()};
	if (!firstAttributeOffsetValid(h.firstAttributeOffset, h.usedSize, raw.size())) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = 0x14};
	}
	return h;
}

} // namespace

Result<RecordHeader> readRecordHeader(std::span<const std::byte> raw) {
	if (!signatureIsFile(raw)) {
		return Error{.code = ErrorCode::kNotFound, .offset = 0};
	}
	if (raw.size() < kRecordHeaderSize) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = raw.size()};
	}
	return readRecordHeaderFields(raw);
}

MftRecordView recordShell(std::uint64_t number, const RecordHeader& h) {
	MftRecordView view;
	view.recordNumber = number;
	view.inUse = (h.flags & kInUseFlag) != 0;
	view.isDirectory = (h.flags & kDirectoryFlag) != 0;
	view.sequence = h.sequence;
	return view;
}

Result<MftRecordView>
recordViewFromFixup(std::uint64_t recordNumber, const RecordHeader& h, FixupOutcome fixup) {
	MftRecordView view = recordShell(recordNumber, h);
	if (h.baseRecord != 0 || !fixup.applied) {
		view.grade = Confidence::kUncertain;
		view.fixedUp = std::move(fixup.fixedUp);
		return {std::move(view)};
	}
	view.fixedUp = std::move(fixup.fixedUp);
	view.grade = parseRecordAttributes(view, view.fixedUp, h);
	return {std::move(view)};
}

Result<MftRecordView> parseMftRecord(std::span<const std::byte> raw, std::uint64_t recordNumber) {
	if (!signatureIsFile(raw)) {
		return Error{.code = ErrorCode::kNotFound, .offset = 0};
	}
	return readRecordHeader(raw).andThen([recordNumber, &raw](const RecordHeader& h) {
		return applyUpdateSequenceFixup(raw).andThen([&h, recordNumber](FixupOutcome fixup) {
			return recordViewFromFixup(recordNumber, h, std::move(fixup));
		});
	});
}

} // namespace revenant::fs::ntfs

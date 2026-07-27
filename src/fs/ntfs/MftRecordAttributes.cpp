// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "MftAttributes.hpp"
#include "MftRecordInternal.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/fs/ntfs/MftRecord.hpp"

namespace revenant::fs::ntfs {

namespace {

constexpr std::uint32_t kAttributeEnd = 0xFFFFFFFFU;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) - offset/length/usedSize are semantically
// distinct.
[[nodiscard]] bool
attributeFits(std::uint64_t offset, std::uint32_t length, std::uint32_t usedSize) {
	return offset + static_cast<std::uint64_t>(length) <= usedSize;
}

[[nodiscard]] bool consumeStandardInformation(
	MftRecordView& view,
	std::span<const std::byte> record,
	const AttributeView& attr) {
	const ByteReader reader{record};
	const auto content = reader.bytes(attr.offset + attr.contentOffset, attr.contentLength);
	if (!content.hasValue()) {
		return false;
	}
	auto parsed = parseStandardInformation(content.value());
	if (parsed.hasValue()) {
		view.standardInfo = parsed.value();
	}
	return parsed.hasValue();
}

[[nodiscard]] bool
consumeFileName(MftRecordView& view, std::span<const std::byte> record, const AttributeView& attr) {
	const ByteReader reader{record};
	const auto content = reader.bytes(attr.offset + attr.contentOffset, attr.contentLength);
	if (!content.hasValue()) {
		return false;
	}
	auto parsed = parseFileName(content.value());
	if (parsed.hasValue()) {
		view.names.push_back(std::move(parsed.value()));
	}
	return parsed.hasValue();
}

[[nodiscard]] bool
consumeData(MftRecordView& view, std::span<const std::byte> record, const AttributeView& attr) {
	if (attr.nameLength != 0 || view.data.has_value()) {
		return true;
	}
	auto parsed = parseDataAttribute(attr, record);
	if (parsed.hasValue()) {
		view.data = std::move(parsed.value());
	}
	return parsed.hasValue();
}

[[nodiscard]] bool consumeAttribute(
	MftRecordView& view,
	std::span<const std::byte> record,
	const AttributeView& attr) {
	if (attr.type == 0x10) {
		return consumeStandardInformation(view, record, attr);
	}
	if (attr.type == 0x30) {
		return consumeFileName(view, record, attr);
	}
	if (attr.type == 0x80) {
		return consumeData(view, record, attr);
	}
	return true;
}

[[nodiscard]] bool consumeOneAttribute(
	MftRecordView& view,
	std::span<const std::byte> record,
	const RecordHeader& h,
	std::uint64_t& offset) {
	const auto attr = readAttributeView(record, offset);
	if (!attr.hasValue() || attr.value().type == kAttributeEnd) {
		return attr.hasValue();
	}
	const auto& a = attr.value();
	if (!attributeFits(a.offset, a.length, h.usedSize) || !consumeAttribute(view, record, a)) {
		return false;
	}
	offset += a.length;
	return true;
}

} // namespace

Confidence parseRecordAttributes(
	MftRecordView& view,
	std::span<const std::byte> record,
	const RecordHeader& h) {
	auto offset = static_cast<std::uint64_t>(h.firstAttributeOffset);
	bool uncertain = false;
	while (offset < h.usedSize) {
		if (!consumeOneAttribute(view, record, h, offset)) {
			uncertain = true;
			break;
		}
	}
	return uncertain ? Confidence::kUncertain : Confidence::kValid;
}

} // namespace revenant::fs::ntfs

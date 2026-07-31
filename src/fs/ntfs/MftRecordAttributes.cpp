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

// Reading one attribute's content, parsing it, and keeping what parsed is one
// protocol with two hooks: which parser the type calls for, and where the
// result belongs on the view. Two attribute types share it; a third
// (`consumeData`) does not, because it is handed the whole record instead.
template <typename Parse, typename Store>
[[nodiscard]] bool consumeContent(
	std::span<const std::byte> record,
	const AttributeView& attr,
	Parse parse,
	Store store) {
	const ByteReader reader{record};
	const auto content = reader.bytes(attr.offset + attr.contentOffset, attr.contentLength);
	if (!content.hasValue()) {
		return false;
	}
	auto parsed = parse(content.value());
	if (parsed.hasValue()) {
		store(std::move(parsed.value()));
	}
	return parsed.hasValue();
}

[[nodiscard]] bool consumeStandardInformation(
	MftRecordView& view,
	std::span<const std::byte> record,
	const AttributeView& attr) {
	return consumeContent(record, attr, parseStandardInformation, [&view](Timestamps stamps) {
		view.standardInfo = stamps;
	});
}

[[nodiscard]] bool
consumeFileName(MftRecordView& view, std::span<const std::byte> record, const AttributeView& attr) {
	return consumeContent(record, attr, parseFileName, [&view](MftFileName name) {
		view.names.push_back(std::move(name));
	});
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

// Why this is not a bool: the end marker is a *successful* stop, but it does not
// advance `offset`. Reporting it as plain success let the caller's loop re-read
// the same marker forever whenever it sat below usedSize.
enum class AttributeStep : std::uint8_t { kContinue, kEnd, kInvalid };

[[nodiscard]] AttributeStep classifyAttribute(
	MftRecordView& view,
	std::span<const std::byte> record,
	const RecordHeader& h,
	const AttributeView& attr) {
	if (attr.type == kAttributeEnd) {
		return AttributeStep::kEnd;
	}
	if (!attributeFits(attr.offset, attr.length, h.usedSize) ||
		!consumeAttribute(view, record, attr)) {
		return AttributeStep::kInvalid;
	}
	return AttributeStep::kContinue;
}

// Advances `offset` only on kContinue, so a stop of either kind cannot loop.
[[nodiscard]] AttributeStep consumeOneAttribute(
	MftRecordView& view,
	std::span<const std::byte> record,
	const RecordHeader& h,
	std::uint64_t& offset) {
	const auto attr = readAttributeView(record, offset);
	if (!attr.hasValue()) {
		return AttributeStep::kInvalid;
	}
	const auto step = classifyAttribute(view, record, h, attr.value());
	if (step == AttributeStep::kContinue) {
		offset += attr.value().length;
	}
	return step;
}

[[nodiscard]] Confidence gradeFor(AttributeStep step) {
	return step == AttributeStep::kInvalid ? Confidence::kUncertain : Confidence::kValid;
}

} // namespace

Confidence parseRecordAttributes(
	MftRecordView& view,
	std::span<const std::byte> record,
	const RecordHeader& h) {
	auto offset = static_cast<std::uint64_t>(h.firstAttributeOffset);
	auto step = AttributeStep::kContinue;
	while (step == AttributeStep::kContinue && offset < h.usedSize) {
		step = consumeOneAttribute(view, record, h, offset);
	}
	return gradeFor(step);
}

} // namespace revenant::fs::ntfs

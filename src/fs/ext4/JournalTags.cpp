// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fs/SlotReader.hpp"
#include "fs/ext4/JournalFormat.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::ext4 {

namespace {

constexpr std::size_t kBlockTypeOffset = 0x04;
constexpr std::size_t kMagicOffset = 0x00;

// Checksum-v2 and -v3 journals keep a four-byte checksum at the end of a
// descriptor block. It is not a tag and reading it as one would invent a block.
constexpr std::size_t kBlockTailBytes = 4;

// The UUID that follows a tag whose set the flag does not claim it shares.
constexpr std::size_t kTagUuidBytes = 16;

constexpr std::uint32_t kSameUuidFlag = 0x2;
constexpr std::uint32_t kLastTagFlag = 0x8;

constexpr std::size_t kChecksumV3TagBytes = 16;

// Where a tag's fields sit. Checksum-v3 widened flags to 32 bits and moved the
// high half of the block number after them; the older shape packs a 16-bit
// checksum and 16-bit flags into the same four bytes.
struct TagFields {
	std::uint64_t block;
	std::uint32_t flags;
};

[[nodiscard]] TagFields checksumV3Tag(const ByteReader& reader) {
	const std::uint64_t high = slotFieldBeAt<std::uint32_t>(reader, 0x08);
	return TagFields{
		.block = (high << 32U) | slotFieldBeAt<std::uint32_t>(reader, 0x00),
		.flags = slotFieldBeAt<std::uint32_t>(reader, 0x04)};
}

[[nodiscard]] TagFields classicTag(const ByteReader& reader, std::size_t tagBytes) {
	const std::uint64_t high =
		tagBytes > 0x08 ? slotFieldBeAt<std::uint32_t>(reader, 0x08) : std::uint32_t{0};
	return TagFields{
		.block = (high << 32U) | slotFieldBeAt<std::uint32_t>(reader, 0x00),
		.flags = slotFieldBeAt<std::uint16_t>(reader, 0x06)};
}

[[nodiscard]] TagFields tagAt(std::span<const std::byte> tag, std::size_t tagBytes) {
	const ByteReader reader{tag};
	return tagBytes == kChecksumV3TagBytes ? checksumV3Tag(reader) : classicTag(reader, tagBytes);
}

// How far the next tag sits: past this one, and past the UUID a tag carries
// unless it says it shares the set's.
[[nodiscard]] std::size_t strideOf(const TagFields& fields, std::size_t tagBytes) {
	return tagBytes + ((fields.flags & kSameUuidFlag) != 0 ? 0 : kTagUuidBytes);
}

[[nodiscard]] std::size_t tagLimit(std::span<const std::byte> block, const JournalHead& head) {
	const auto tail = head.hasBlockTail ? kBlockTailBytes : 0;
	return block.size() > tail ? block.size() - tail : 0;
}

[[nodiscard]] Result<bool> isDescriptor(std::span<const std::byte> block) {
	const ByteReader reader{block};
	if (slotFieldBeAt<std::uint32_t>(reader, kMagicOffset) != kJournalMagic) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = kMagicOffset};
	}
	if (slotFieldBeAt<std::uint32_t>(reader, kBlockTypeOffset) != kDescriptorBlockType) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = kBlockTypeOffset};
	}
	return true;
}

// One tag, folded in. Returns how far the next one sits, or 0 when this tag said
// it was the last of its transaction.
[[nodiscard]] std::size_t
takeTag(std::vector<TaggedBlock>& tags, std::span<const std::byte> at, std::size_t tagBytes) {
	const auto fields = tagAt(at.first(tagBytes), tagBytes);
	tags.push_back(
		TaggedBlock{
			.fileSystemBlock = fields.block,
			.blocksAfterDescriptor = static_cast<std::uint32_t>(tags.size() + 1)});
	return (fields.flags & kLastTagFlag) != 0 ? 0 : strideOf(fields, tagBytes);
}

// Reads tags until one says it is the last, or until the next will not fit.
[[nodiscard]] std::vector<TaggedBlock>
readTags(std::span<const std::byte> block, const JournalHead& head) {
	std::vector<TaggedBlock> tags;
	const auto limit = tagLimit(block, head);
	std::size_t at = kJournalHeaderBytes;
	std::size_t stride = head.tagBytes;
	while (stride != 0 && at + head.tagBytes <= limit) {
		stride = takeTag(tags, block.subspan(at), head.tagBytes);
		at += stride;
	}
	return tags;
}

} // namespace

Result<std::vector<TaggedBlock>>
parseDescriptorTags(std::span<const std::byte> block, const JournalHead& head) {
	if (block.size() < kJournalHeaderBytes + head.tagBytes) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = block.size()};
	}
	return isDescriptor(block).map([&](bool) { return readTags(block, head); });
}

} // namespace revenant::fs::ext4

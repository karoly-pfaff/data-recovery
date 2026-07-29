// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <cstdint>
#include <span>

#include "fs/SlotReader.hpp"
#include "fs/ext4/JournalFormat.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::ext4 {

namespace {

// The superblock is 1024 bytes, but everything a reader needs sits in the first
// forty-eight — the fields after them describe transactions this build never
// replays (ADR-0005).
constexpr std::size_t kReadableBytes = 0x30;

constexpr std::size_t kMagicOffset = 0x00;
constexpr std::size_t kBlockTypeOffset = 0x04;
constexpr std::size_t kBlockSizeOffset = 0x0C;
constexpr std::size_t kMaxLengthOffset = 0x10;
constexpr std::size_t kFirstBlockOffset = 0x14;
constexpr std::size_t kIncompatOffset = 0x28;

// The jbd2 incompatible features this build knows the consequences of. REVOKE
// and ASYNC_COMMIT change what a transaction *means*, which a reader that never
// replays one does not care about; the two checksum features change how wide a
// descriptor tag is, which it very much does.
constexpr std::uint32_t kIncompatRevoke = 0x1;
constexpr std::uint32_t kIncompat64Bit = 0x2;
constexpr std::uint32_t kIncompatAsyncCommit = 0x4;
constexpr std::uint32_t kIncompatCsumV2 = 0x8;
constexpr std::uint32_t kIncompatCsumV3 = 0x10;
constexpr std::uint32_t kKnownIncompat =
	kIncompatRevoke | kIncompat64Bit | kIncompatAsyncCommit | kIncompatCsumV2 | kIncompatCsumV3;

// The three tag shapes. Checksum-v3 replaced the packed 16-bit checksum and
// flags with two 32-bit fields; without it, a 64-bit journal appends the high
// half of the block number.
constexpr std::size_t kNarrowTagBytes = 8;
constexpr std::size_t kWideTagBytes = 12;
constexpr std::size_t kChecksumV3TagBytes = 16;

[[nodiscard]] std::size_t tagBytesFor(std::uint32_t features) {
	if ((features & kIncompatCsumV3) != 0) {
		return kChecksumV3TagBytes;
	}
	return (features & kIncompat64Bit) != 0 ? kWideTagBytes : kNarrowTagBytes;
}

[[nodiscard]] Result<std::uint32_t> checkedFeatures(const ByteReader& reader) {
	const auto features = slotFieldBeAt<std::uint32_t>(reader, kIncompatOffset);
	if ((features & ~kKnownIncompat) != 0) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = kIncompatOffset};
	}
	return features;
}

[[nodiscard]] Result<bool> namesJournalSuperblock(const ByteReader& reader) {
	if (slotFieldBeAt<std::uint32_t>(reader, kMagicOffset) != kJournalMagic) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = kMagicOffset};
	}
	const auto type = slotFieldBeAt<std::uint32_t>(reader, kBlockTypeOffset);
	if (type != kSuperblockV1Type && type != kSuperblockV2Type) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = kBlockTypeOffset};
	}
	return true;
}

[[nodiscard]] JournalHead headOf(const ByteReader& reader, std::uint32_t features) {
	return JournalHead{
		.blockSizeBytes = slotFieldBeAt<std::uint32_t>(reader, kBlockSizeOffset),
		.maxBlocks = slotFieldBeAt<std::uint32_t>(reader, kMaxLengthOffset),
		.firstBlock = slotFieldBeAt<std::uint32_t>(reader, kFirstBlockOffset),
		.tagBytes = tagBytesFor(features),
		.hasBlockTail = (features & (kIncompatCsumV2 | kIncompatCsumV3)) != 0};
}

} // namespace

Result<JournalHead> parseJournalSuperblock(std::span<const std::byte> block) {
	return slotReader(block, kReadableBytes).andThen([](const ByteReader& reader) {
		return namesJournalSuperblock(reader)
			.andThen([&reader](bool) { return checkedFeatures(reader); })
			.map([&reader](std::uint32_t features) { return headOf(reader, features); });
	});
}

} // namespace revenant::fs::ext4

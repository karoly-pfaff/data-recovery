// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ntfs/MftRecordBuilder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "imagegen/ByteWriter.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/core/ByteReader.hpp"

namespace revenant::imagegen::ntfs {

namespace {

constexpr std::size_t kStrideBytes = 512;
constexpr std::size_t kUsaStart = 0x30;
constexpr std::size_t kFirstAttributeOffset = 0x38;
constexpr std::uint16_t kInUseFlag = 0x01;
constexpr std::uint16_t kDirectoryFlag = 0x02;

// Record header field positions.
constexpr std::size_t kSignatureOffset = 0x00;
constexpr std::size_t kUsaOffsetField = 0x04;
constexpr std::size_t kUsaCountOffset = 0x06;
constexpr std::size_t kSequenceOffset = 0x10;
constexpr std::size_t kFirstAttributeOffsetOffset = 0x14;
constexpr std::size_t kFlagsOffset = 0x16;
constexpr std::size_t kUsedSizeOffset = 0x18;
constexpr std::size_t kAllocatedSizeOffset = 0x1C;

constexpr std::array<std::byte, 4> kFileSignature{
	std::byte{'F'},
	std::byte{'I'},
	std::byte{'L'},
	std::byte{'E'}};

// One saved word per 512-byte stride, plus the update sequence number itself.
[[nodiscard]] std::uint16_t usaCount(const NtfsLayout& layout) noexcept {
	return static_cast<std::uint16_t>((layout.mftRecordBytes / kStrideBytes) + 1);
}

[[nodiscard]] std::uint16_t recordFlags(const MftRecordSpec& spec) noexcept {
	const std::uint16_t inUse = spec.inUse ? kInUseFlag : 0;
	return static_cast<std::uint16_t>(inUse | (spec.isDirectory ? kDirectoryFlag : 0));
}

[[nodiscard]] std::uint32_t usedSize(const MftRecordSpec& spec) noexcept {
	return static_cast<std::uint32_t>(kFirstAttributeOffset + spec.attributes.size());
}

void putHeader(
	std::vector<std::byte>& record,
	const MftRecordSpec& spec,
	const NtfsLayout& layout) {
	putBytes(record, kSignatureOffset, kFileSignature);
	putLe<std::uint16_t>(record, kUsaOffsetField, kUsaStart);
	putLe<std::uint16_t>(record, kUsaCountOffset, usaCount(layout));
	putLe<std::uint16_t>(record, kSequenceOffset, spec.sequence);
	putLe<std::uint16_t>(record, kFirstAttributeOffsetOffset, kFirstAttributeOffset);
	putLe<std::uint16_t>(record, kFlagsOffset, recordFlags(spec));
	putLe<std::uint32_t>(record, kUsedSizeOffset, usedSize(spec));
	putLe<std::uint32_t>(record, kAllocatedSizeOffset, layout.mftRecordBytes);
}

// Saves the word a stride tail holds and replaces it with the update sequence
// number — the on-disk torn-write detector the parser reverses.
void stashStrideTail(std::vector<std::byte>& record, std::size_t stride) {
	const auto tail = ((stride + 1) * kStrideBytes) - 2;
	const auto saved = revenant::ByteReader{record}.readLe<std::uint16_t>(tail).value();
	putLe<std::uint16_t>(record, kUsaStart + 2 + (stride * 2), saved);
	putLe<std::uint16_t>(record, tail, kUpdateSequenceNumber);
}

void applyUpdateSequence(std::vector<std::byte>& record, const NtfsLayout& layout) {
	putLe<std::uint16_t>(record, kUsaStart, kUpdateSequenceNumber);
	for (std::size_t stride = 0; stride + 1 < usaCount(layout); ++stride) {
		stashStrideTail(record, stride);
	}
}

} // namespace

std::vector<std::byte> buildMftRecord(const NtfsLayout& layout, const MftRecordSpec& spec) {
	std::vector<std::byte> record(layout.mftRecordBytes, std::byte{0});
	putHeader(record, spec, layout);
	putBytes(record, kFirstAttributeOffset, spec.attributes);
	applyUpdateSequence(record, layout);
	return record;
}

} // namespace revenant::imagegen::ntfs

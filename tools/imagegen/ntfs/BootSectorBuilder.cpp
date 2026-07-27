// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ntfs/BootSectorBuilder.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "imagegen/ntfs/ByteWriter.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"

namespace revenant::imagegen::ntfs {

namespace {

// On-disk field positions, named so the writer below reads as a field list
// rather than as arithmetic.
constexpr std::size_t kOemIdOffset = 0x03;
constexpr std::size_t kBytesPerSectorOffset = 0x0B;
constexpr std::size_t kSectorsPerClusterOffset = 0x0D;
constexpr std::size_t kTotalSectorsOffset = 0x28;
constexpr std::size_t kMftClusterOffset = 0x30;
constexpr std::size_t kRecordSizeOffset = 0x40;
constexpr std::size_t kSignatureOffset = 0x1FE;

// Stated here independently of the parser's own copy of these constants: a
// fixture that borrowed them could not catch a parser that had them wrong.
constexpr std::string_view kOemId = "NTFS    ";
constexpr std::array<std::byte, 2> kSignature{std::byte{0x55}, std::byte{0xAA}};

[[nodiscard]] std::span<const std::byte> oemIdBytes() noexcept {
	return std::as_bytes(std::span{kOemId.data(), kOemId.size()});
}

// A record smaller than a cluster is spelled as the negative log2 of its size,
// which is how every real NTFS volume with 4 KiB clusters writes this field.
[[nodiscard]] std::uint8_t recordSizeCode(std::uint32_t recordBytes) noexcept {
	std::int8_t shift = 0;
	while ((std::uint32_t{1} << static_cast<std::uint32_t>(shift)) < recordBytes) {
		++shift;
	}
	return std::bit_cast<std::uint8_t>(static_cast<std::int8_t>(-shift));
}

} // namespace

std::vector<std::byte> buildBootSector(const NtfsLayout& layout) {
	std::vector<std::byte> sector(layout.bytesPerSector, std::byte{0});
	putBytes(sector, kOemIdOffset, oemIdBytes());
	putLe<std::uint16_t>(
		sector,
		kBytesPerSectorOffset,
		static_cast<std::uint16_t>(layout.bytesPerSector));
	putLe<std::uint8_t>(
		sector,
		kSectorsPerClusterOffset,
		static_cast<std::uint8_t>(layout.sectorsPerCluster));
	putLe<std::uint64_t>(
		sector,
		kTotalSectorsOffset,
		layout.totalClusters * layout.sectorsPerCluster);
	putLe<std::uint64_t>(sector, kMftClusterOffset, layout.mftStartCluster);
	putLe<std::uint8_t>(sector, kRecordSizeOffset, recordSizeCode(layout.mftRecordBytes));
	putBytes(sector, kSignatureOffset, kSignature);
	return sector;
}

} // namespace revenant::imagegen::ntfs

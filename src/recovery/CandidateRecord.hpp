// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The on-disk shape of a candidate index: file names, the header,
// and the fixed-size record. Shared by the writer and the reader so the two
// cannot drift apart. Not a public interface.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace revenant::recovery {

inline constexpr std::string_view kIndexFileName = "candidates.idx";
inline constexpr std::string_view kBlobFileName = "candidates.dat";

// A magic and a version, so a foreign or newer index is refused rather than
// misread (ADR-0008).
inline constexpr std::array<std::byte, 8> kIndexMagic{
	std::byte{'R'},
	std::byte{'V'},
	std::byte{'N'},
	std::byte{'T'},
	std::byte{'I'},
	std::byte{'D'},
	std::byte{'X'},
	std::byte{0x00}};
inline constexpr std::uint32_t kIndexVersion = 1;

// magic(8) + version(4) + record size(4). The record size is written so a
// reader can reject a file whose stride it does not agree with, instead of
// slicing it at the wrong boundaries.
inline constexpr std::size_t kIndexHeaderBytes = 16;
inline constexpr std::size_t kRecordBytes = 48;

// The fixed part of one candidate: where its variable-length parts live in the
// blob, plus everything that is the same width for every candidate. The blob
// entry is `nameLength` name bytes, then `extentCount` 16-byte extents, then
// `residentLength` content bytes.
struct CandidateRecord {
	std::uint64_t blobOffset;
	std::uint64_t created;
	std::uint64_t modified;
	std::uint64_t accessed;
	std::uint32_t nameLength;
	std::uint32_t residentLength;
	std::uint32_t extentCount;
	std::uint8_t confidence;
	std::uint8_t source;
};

// Bytes per encoded extent in the blob: offset then length, both 64-bit.
inline constexpr std::size_t kExtentBytes = 16;

[[nodiscard]] std::array<std::byte, kIndexHeaderBytes> encodeIndexHeader();
[[nodiscard]] bool headerIsOurs(std::span<const std::byte> raw);

[[nodiscard]] std::array<std::byte, kRecordBytes> encodeRecord(const CandidateRecord& record);
[[nodiscard]] CandidateRecord decodeRecord(std::span<const std::byte> raw);

} // namespace revenant::recovery

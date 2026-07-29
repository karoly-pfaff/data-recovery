// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The GPT header's validators, one per rule, each reporting the byte
// offset of the field it rejected, plus the two derivations the device-level
// read needs. Not a public interface.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/volume/Gpt.hpp"
#include "revenant/volume/GptPartitions.hpp"

namespace revenant::volume {

// Whether the sector names the format at all. This is the question a scheme
// choice probes with, so it is answered before anything is measured.
[[nodiscard]] Result<bool> namesGpt(const ByteReader& reader);

// How many bytes the header claims to occupy, checked against what is actually
// there: below the specified minimum, or past the end of the sector, and the
// checksum could not be computed over it honestly.
[[nodiscard]] Result<std::size_t> headerSizeIn(const ByteReader& reader, std::size_t available);

// Whether `header` checksums to the value it carries. The four bytes of the
// field itself count as zero, which is how the value was computed when it was
// written.
[[nodiscard]] Result<bool> checksumIsValid(std::span<const std::byte> header);

// Whether the header agrees it belongs at `atLba`. The primary and the backup
// are byte-identical but for this field and its twin, so a copy believed at the
// wrong place would send the read to the other copy's entry array.
[[nodiscard]] Result<bool> placedAt(const ByteReader& reader, std::uint64_t atLba);

// The remaining fields, each already past its own rule.
[[nodiscard]] Result<GptHeader> headerBodyOf(const ByteReader& reader);

// The entry array `header` vouches for, read off `device` and verified against
// the CRC32 the header states. A short read, an array outside the device, or a
// checksum that does not match is that failure's typed error.
[[nodiscard]] Result<std::vector<std::byte>>
readEntryArray(BlockDevice& device, const GptHeader& header);

// Every used slot in `array`, restated as byte ranges at `sectorSize` bytes per
// sector. A slot that will not parse fails the whole array: its bytes already
// passed the CRC the header vouched for them with, so bytes that still do not
// make sense mean this copy of the table is not one to trust.
[[nodiscard]] Result<std::vector<GptPartition>>
partitionsIn(std::span<const std::byte> array, const GptHeader& header, std::uint32_t sectorSize);

} // namespace revenant::volume

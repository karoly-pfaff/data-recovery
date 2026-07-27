// SPDX-License-Identifier: GPL-3.0-or-later
// Internal. MFT attribute header parsing and $STANDARD_INFORMATION,
// $FILE_NAME, $DATA content extraction. Not a public interface.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ntfs/MftRecord.hpp"

namespace revenant::fs::ntfs {

// Raw attribute header plus the derived content/range fields. Offsets are
// relative to the start of the attribute in the fixed-up record.
struct AttributeView {
	std::uint32_t type{};
	std::uint32_t length{};
	std::uint64_t offset{}; // start of this attribute in the record
	bool nonResident{};
	std::uint8_t nameLength{};
	std::uint16_t nameOffset{};
	std::size_t contentLength{};   // resident: actual content bytes
	std::uint16_t contentOffset{}; // resident: offset to content from attribute start
	std::uint16_t runlistOffset{}; // non-resident: offset to runlist bytes
	std::uint64_t realSize{};      // non-resident: declared content size
};

// Read the attribute header at `offset` within `record`. The caller has
// already validated that `offset + length` fits inside the used area.
[[nodiscard]] Result<AttributeView>
readAttributeView(std::span<const std::byte> record, std::uint64_t offset);

// Parse the content of the three attribute types we care about.
[[nodiscard]] Result<Timestamps> parseStandardInformation(std::span<const std::byte> content);
[[nodiscard]] Result<MftFileName> parseFileName(std::span<const std::byte> content);
[[nodiscard]] Result<MftData>
parseDataAttribute(const AttributeView& view, std::span<const std::byte> record);

} // namespace revenant::fs::ntfs

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "revenant/core/Confidence.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/NameDecode.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::fs::ntfs {

// One $FILE_NAME attribute: parent record reference, decoded name, and size.
struct MftFileName {
	std::uint64_t parentRecord{};   // low 48 bits of parent reference
	std::uint16_t parentSequence{}; // high 16 bits of parent reference
	std::uint8_t nameSpace{};       // 0 POSIX, 1 Win32, 2 DOS, 3 Win32|DOS
	DecodedName name;
	std::uint64_t realSize{};
};

// $DATA attribute: resident payload, or a view into the record's fixed-up bytes
// that holds the non-resident runlist to decode later.
struct MftData {
	bool resident{};
	std::vector<std::byte> residentContent;  // valid when resident is true
	std::span<const std::byte> runlistBytes; // valid when resident is false
	std::uint64_t realSize{};

	MftData() = default;
	MftData(const MftData&) = delete;
	MftData& operator=(const MftData&) = delete;
	MftData(MftData&&) = default;
	MftData& operator=(MftData&&) = default;
	~MftData() = default;
};

// Parsed view of one MFT record. The fixed-up copy of the on-disk record is
// owned here so that `runlistBytes` always points at valid memory as long as
// the view is not copied (it is move-only).
struct MftRecordView {
	std::uint64_t recordNumber{};
	bool inUse{};
	bool isDirectory{};
	std::uint16_t sequence{};
	Confidence grade{};
	std::optional<Timestamps> standardInfo;
	std::vector<MftFileName> names;
	std::optional<MftData> data;
	std::vector<std::byte> fixedUp;

	MftRecordView() = default;
	MftRecordView(const MftRecordView&) = delete;
	MftRecordView& operator=(const MftRecordView&) = delete;
	MftRecordView(MftRecordView&&) = default;
	MftRecordView& operator=(MftRecordView&&) = default;
	~MftRecordView() = default;
};

// Parses and validates one MFT record from `raw` (must be exactly one record
// size). `recordNumber` is used for provenance and grading messages; the
// parser does not read record 0's runlist. Unparseable/no-signature records
// yield kNotFound so the caller can skip them (carve territory).
[[nodiscard]] Result<MftRecordView>
parseMftRecord(std::span<const std::byte> raw, std::uint64_t recordNumber);

} // namespace revenant::fs::ntfs

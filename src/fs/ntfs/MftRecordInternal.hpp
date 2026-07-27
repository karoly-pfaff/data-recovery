// SPDX-License-Identifier: GPL-3.0-or-later
// Internal helpers shared by MftRecord.cpp, MftRecordFixup.cpp and
// MftRecordAttributes.cpp. Not a public interface.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Confidence.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/ntfs/MftRecord.hpp"

namespace revenant::fs::ntfs {

struct RecordHeader {
	std::uint16_t usaOffset{};
	std::uint16_t usaCount{};
	std::uint16_t sequence{};
	std::uint16_t firstAttributeOffset{};
	std::uint16_t flags{};
	std::uint32_t usedSize{};
	std::uint64_t baseRecord{};
};

struct FixupOutcome {
	bool applied{};
	std::vector<std::byte> fixedUp;
};

[[nodiscard]] Result<RecordHeader> readRecordHeader(std::span<const std::byte> raw);
[[nodiscard]] Result<FixupOutcome> applyUpdateSequenceFixup(std::span<const std::byte> raw);

[[nodiscard]] MftRecordView recordShell(std::uint64_t number, const RecordHeader& h);
[[nodiscard]] Confidence parseRecordAttributes(
	MftRecordView& view,
	std::span<const std::byte> record,
	const RecordHeader& h);
[[nodiscard]] Result<MftRecordView>
recordViewFromFixup(std::uint64_t recordNumber, const RecordHeader& h, FixupOutcome fixup);

} // namespace revenant::fs::ntfs

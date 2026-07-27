// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "imagegen/ntfs/NtfsLayout.hpp"

namespace revenant::imagegen::ntfs {

// The fixed update sequence number stamped into every stride tail. A constant
// (rather than a counter) keeps the fixture byte-identical across runs.
inline constexpr std::uint16_t kUpdateSequenceNumber = 0x0A0A;

struct MftRecordSpec {
	std::uint16_t sequence{};
	bool inUse{};
	bool isDirectory{};
	std::span<const std::byte> attributes; // concatenated, end marker included
};

// Builds one `layout.mftRecordBytes` record: `FILE` header, the attributes as
// given, and an update sequence array applied the way a real NTFS volume
// writes it — so the production parser has a fixup to undo, not a formality.
[[nodiscard]] std::vector<std::byte>
buildMftRecord(const NtfsLayout& layout, const MftRecordSpec& spec);

} // namespace revenant::imagegen::ntfs

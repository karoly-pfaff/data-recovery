// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Result.hpp"

namespace revenant::fs::ntfs {

// Validated NTFS boot-sector geometry. All on-disk fields are checked, so the
// returned values are safe to multiply and use as byte offsets/sizes.
struct NtfsGeometry {
	std::uint32_t bytesPerSector;
	std::uint32_t bytesPerCluster;
	std::uint64_t totalClusters;
	std::uint64_t mftOffsetBytes;
	std::uint32_t bytesPerMftRecord;
};

// Parses and validates a 512-byte NTFS boot sector. Truncated input yields
// kOutOfRange; any on-disk rule violation yields kInvalidArgument carrying the
// field's byte offset.
[[nodiscard]] Result<NtfsGeometry> parseBootSector(std::span<const std::byte> sector);

} // namespace revenant::fs::ntfs

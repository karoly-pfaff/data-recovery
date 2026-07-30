// SPDX-License-Identifier: GPL-3.0-or-later
// Internal helpers shared by BootSector.cpp and BootSectorFields.cpp: the
// per-field readers of the NTFS boot sector. The overflow-checked arithmetic
// they derive geometry with is shared with every other on-disk geometry parser
// and lives in revenant/core/SafeArith.hpp. Not a public interface.
#pragma once

#include <cstdint>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::ntfs {

// One reader per on-disk field, each validating its own row of the table and
// reporting the field's byte offset on rejection. The fields NTFS shares with
// FAT32 — sector size, cluster size, and the boot signature — come from
// fs/BpbFields.hpp and are named unqualified here.
[[nodiscard]] Result<bool> oemIdIsValid(const ByteReader& reader);
[[nodiscard]] Result<std::uint64_t> totalSectors(const ByteReader& reader);
[[nodiscard]] Result<std::uint64_t> mftClusterNumber(const ByteReader& reader);
[[nodiscard]] Result<std::uint32_t>
mftRecordSize(const ByteReader& reader, std::uint32_t bytesPerCluster);

} // namespace revenant::fs::ntfs

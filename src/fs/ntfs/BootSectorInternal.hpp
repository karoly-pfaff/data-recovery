// SPDX-License-Identifier: GPL-3.0-or-later
// Internal helpers shared by BootSector.cpp and BootSectorFields.cpp: the
// per-field readers of the NTFS boot sector plus the overflow-checked
// multiplications that derive geometry from them. Not a public interface.
#pragma once

#include <cstdint>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::ntfs {

// Overflow-checked multiplication. `offset` is the boot-sector byte offset
// reported on the error, not an operand.
[[nodiscard]] Result<std::uint32_t>
safeMul32(std::uint32_t a, std::uint32_t b, std::uint64_t offset) noexcept;
[[nodiscard]] Result<std::uint64_t>
safeMul64(std::uint64_t a, std::uint64_t b, std::uint64_t offset) noexcept;

// One reader per on-disk field, each validating its own row of the table and
// reporting the field's byte offset on rejection.
[[nodiscard]] Result<bool> oemIdIsValid(const ByteReader& reader);
[[nodiscard]] Result<std::uint32_t> bytesPerSector(const ByteReader& reader);
[[nodiscard]] Result<std::uint32_t> sectorsPerCluster(const ByteReader& reader);
[[nodiscard]] Result<std::uint64_t> totalSectors(const ByteReader& reader);
[[nodiscard]] Result<std::uint64_t> mftClusterNumber(const ByteReader& reader);
[[nodiscard]] Result<std::uint32_t>
mftRecordSize(const ByteReader& reader, std::uint32_t bytesPerCluster);
[[nodiscard]] Result<bool> signatureIsValid(const ByteReader& reader);

} // namespace revenant::fs::ntfs

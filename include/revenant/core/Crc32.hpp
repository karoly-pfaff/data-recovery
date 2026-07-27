// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace revenant {

// IEEE 802.3 CRC-32 (polynomial 0xEDB88320, reflected) — the checksum PNG
// chunks and ZIP entries both carry. Validation-grade: it tells a real file
// from a plausible-looking one, which is the whole point of ADR-0003 carving.
[[nodiscard]] std::uint32_t crc32(std::span<const std::byte> data) noexcept;

} // namespace revenant

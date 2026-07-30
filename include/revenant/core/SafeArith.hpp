// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Overflow-checked arithmetic over on-disk geometry fields: every product and
// sum derived from untrusted numbers goes through here, so no crafted boot
// sector can wrap a byte offset into a small one. `offset` is the field's byte
// position, reported on the error — never an operand. Where the failure has no
// byte position to name — a product of two numbers that were themselves
// derived — callers pass 0.
//
// Shared by every on-disk geometry parser, filesystem and partition table
// alike.

#include <cstdint>

#include "revenant/core/Result.hpp"

namespace revenant {

[[nodiscard]] Result<std::uint32_t>
safeMul32(std::uint32_t a, std::uint32_t b, std::uint64_t offset) noexcept;

[[nodiscard]] Result<std::uint64_t>
safeMul64(std::uint64_t a, std::uint64_t b, std::uint64_t offset) noexcept;

[[nodiscard]] Result<std::uint64_t>
safeAdd64(std::uint64_t a, std::uint64_t b, std::uint64_t offset) noexcept;

} // namespace revenant

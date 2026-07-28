// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Overflow-checked arithmetic over on-disk geometry fields: every
// product and sum derived from untrusted numbers goes through here, so no
// crafted boot sector can wrap a byte offset into a small one. `offset` is the
// field's byte position, reported on the error — never an operand.
//
// Shared by every filesystem's geometry parser; not a public interface.

#include <cstdint>

#include "revenant/core/Result.hpp"

namespace revenant::fs {

[[nodiscard]] Result<std::uint32_t>
safeMul32(std::uint32_t a, std::uint32_t b, std::uint64_t offset) noexcept;

[[nodiscard]] Result<std::uint64_t>
safeMul64(std::uint64_t a, std::uint64_t b, std::uint64_t offset) noexcept;

[[nodiscard]] Result<std::uint64_t>
safeAdd64(std::uint64_t a, std::uint64_t b, std::uint64_t offset) noexcept;

} // namespace revenant::fs

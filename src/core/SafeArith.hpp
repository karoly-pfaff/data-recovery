// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Overflow-checked arithmetic over on-disk geometry fields: every
// product and sum derived from untrusted numbers goes through here, so no
// crafted boot sector can wrap a byte offset into a small one. `offset` is the
// field's byte position, reported on the error — never an operand. Where the
// failure has no byte position to name — a product of two numbers that were
// themselves derived — callers pass 0.
//
// Shared by every on-disk geometry parser, filesystem and partition table
// alike; not a public interface — see the include-path rule in
// src/CMakeLists.txt.

#include <cstdint>

#include "revenant/core/Result.hpp"

namespace revenant {

[[nodiscard]] Result<std::uint32_t>
safeMul32(std::uint32_t a, std::uint32_t b, std::uint64_t offset) noexcept;

[[nodiscard]] Result<std::uint64_t>
safeMul64(std::uint64_t a, std::uint64_t b, std::uint64_t offset) noexcept;

[[nodiscard]] Result<std::uint64_t>
safeAdd64(std::uint64_t a, std::uint64_t b, std::uint64_t offset) noexcept;

// `a + b`, pinned at the maximum instead of wrapping — for the callers that
// have nowhere to put an error.
//
// Restating a byte range in another coordinate system is one of those: it
// happens while a run is being *reported*, where a `Result` could only be
// discarded, and where a wrap is the one outcome that must not occur. A range
// that wrapped to a small number would sit somewhere real and be compared
// against real things; one pinned at the maximum lies past the end of any
// device, intersects nothing, and is merely useless.
[[nodiscard]] std::uint64_t saturatingAdd64(std::uint64_t a, std::uint64_t b) noexcept;

} // namespace revenant

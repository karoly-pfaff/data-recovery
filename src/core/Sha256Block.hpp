// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The SHA-256 compression function, split from the streaming state
// because folding one block and deciding *which* blocks to fold are two jobs.
// Not a public interface.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Sha256.hpp"

namespace revenant::detail {

// One 64-byte block folded into the eight-word state (FIPS 180-4 §6.2.2).
void compressSha256Block(
	std::array<std::uint32_t, kSha256StateWords>& state,
	std::span<const std::byte, kSha256BlockBytes> block);

} // namespace revenant::detail

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to src/carve/ — the vectorized half of the matcher's reject step.
// Its translation unit is the only one compiled with AVX2 enabled, and nothing
// here is called before `cpuHasAvx2()` has said yes.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/carve/SignatureTable.hpp"

namespace revenant::carve {

// One vector's worth of positions, and how many vectors one call answers for.
//
// Four rather than one because every call crosses out of AVX2 code and back,
// which costs an `vzeroupper` and an AVX-to-SSE transition, and because the
// two 16-byte lookup tables have to be broadcast into both lanes on entry.
// Batching amortizes all three over 128 bytes instead of 32.
inline constexpr std::size_t kPrefilterVectorBytes = 32;
inline constexpr std::size_t kPrefilterVectorsPerCall = 4;
inline constexpr std::size_t kPrefilterChunkBytes =
	kPrefilterVectorBytes * kPrefilterVectorsPerCall;

// One bit per byte, lowest bit first, per vector of the chunk.
using SurvivorMasks = std::array<std::uint32_t, kPrefilterVectorsPerCall>;

// Which of `chunk`'s positions the filter lets through. `chunk` must hold at
// least `kPrefilterChunkBytes`.
//
// Conservative in one direction only: a set bit may be a position no signature
// can start at, and a clear bit never is. That is what lets the fast path skip
// bytes without changing what the scan finds.
[[nodiscard]] SurvivorMasks
survivorsAvx2(std::span<const std::byte> chunk, const NibbleFilter& filter) noexcept;

} // namespace revenant::carve

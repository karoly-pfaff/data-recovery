// SPDX-License-Identifier: GPL-3.0-or-later
#include "PrefilterAvx2.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "revenant/carve/SignatureTable.hpp"

#ifdef REVENANT_HAVE_AVX2
#include <immintrin.h>
#endif

// This translation unit alone is compiled with AVX2 enabled (see
// src/CMakeLists.txt). Building the whole binary that way would produce
// something that crashes on a CPU without it, and the machines people run
// recovery tools on are old machines.
namespace revenant::carve {

#ifdef REVENANT_HAVE_AVX2

namespace {

constexpr int kNibbleShift = 4;
constexpr char kLowNibbleMask = 0x0F;

// Vectors are loaded through `memcpy` rather than a cast to `__m256i*`: it is
// the same unaligned move once optimized, and it needs no promise about the
// alignment or the type of bytes that came off a disk (AGENTS.md §3).
template <typename Vector, typename Source>
[[nodiscard]] Vector loadVector(const Source* from) noexcept {
	Vector value{};
	std::memcpy(&value, from, sizeof(value));
	return value;
}

// A 16-byte lookup table in both 128-bit lanes, because `vpshufb` shuffles
// within a lane: the same table has to be present in each for one instruction
// to answer for all 32 bytes.
[[nodiscard]] __m256i inBothLanes(const std::array<std::uint8_t, 16>& table) noexcept {
	return _mm256_broadcastsi128_si256(loadVector<__m128i>(table.data()));
}

// The two broadcast tables and the nibble mask, hoisted out of the per-vector
// work so one call pays for them once.
struct VectorTables {
	__m256i low;
	__m256i high;
	__m256i nibble;
};

[[nodiscard]] VectorTables tablesFor(const NibbleFilter& filter) noexcept {
	return VectorTables{
		.low = inBothLanes(filter.low),
		.high = inBothLanes(filter.high),
		.nibble = _mm256_set1_epi8(kLowNibbleMask)};
}

// One vector: split each byte into nibbles, look both up, and keep the
// positions whose two masks share a bit.
[[nodiscard]] std::uint32_t
survivorsIn(std::span<const std::byte> vector, const VectorTables& tables) noexcept {
	const auto data = loadVector<__m256i>(vector.data());
	const __m256i low = _mm256_shuffle_epi8(tables.low, _mm256_and_si256(data, tables.nibble));
	const __m256i high = _mm256_shuffle_epi8(
		tables.high,
		_mm256_and_si256(_mm256_srli_epi16(data, kNibbleShift), tables.nibble));
	// The mask wanted is the complement of "the two masks shared nothing".
	const __m256i rejected = _mm256_cmpeq_epi8(_mm256_and_si256(low, high), _mm256_setzero_si256());
	return ~static_cast<std::uint32_t>(_mm256_movemask_epi8(rejected));
}

} // namespace

SurvivorMasks survivorsAvx2(std::span<const std::byte> chunk, const NibbleFilter& filter) noexcept {
	const auto tables = tablesFor(filter);
	SurvivorMasks masks{};
	for (std::size_t vector = 0; vector < kPrefilterVectorsPerCall; ++vector) {
		masks.at(vector) = survivorsIn(chunk.subspan(vector * kPrefilterVectorBytes), tables);
	}
	return masks;
}

#else

// No vector unit this build can target. `buildHasAvx2()` reports that, so the
// matcher never chooses the fast path and never calls this; it exists because a
// missing symbol would be a link error on an architecture nobody has asked this
// project to be fast on yet.
SurvivorMasks survivorsAvx2(std::span<const std::byte> chunk, const NibbleFilter& filter) noexcept {
	static_cast<void>(chunk);
	static_cast<void>(filter);
	return SurvivorMasks{};
}

#endif

} // namespace revenant::carve

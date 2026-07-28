// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Sha256Block.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Endian.hpp"
#include "revenant/core/Sha256.hpp"

namespace revenant::detail {

namespace {

constexpr std::size_t kRounds = 64;
constexpr std::size_t kBlockWords = 16;
constexpr std::size_t kWordBytes = sizeof(std::uint32_t);

// FIPS 180-4 §4.2.2: the first 32 bits of the fractional parts of the cube
// roots of the first 64 primes.
constexpr std::array<std::uint32_t, kRounds> kRoundConstants{
	0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
	0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
	0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
	0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
	0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
	0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
	0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
	0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
	0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
	0xc67178f2U};

using Schedule = std::array<std::uint32_t, kRounds>;
using State = std::array<std::uint32_t, kSha256StateWords>;

// The eight working variables of one compression, named as the standard names
// them so the rounds below read like §6.2.2 rather than like array indices.
struct Working {
	std::uint32_t a;
	std::uint32_t b;
	std::uint32_t c;
	std::uint32_t d;
	std::uint32_t e;
	std::uint32_t f;
	std::uint32_t g;
	std::uint32_t h;
};

[[nodiscard]] constexpr std::uint32_t bigSigma0(std::uint32_t x) {
	return std::rotr(x, 2) ^ std::rotr(x, 13) ^ std::rotr(x, 22);
}

[[nodiscard]] constexpr std::uint32_t bigSigma1(std::uint32_t x) {
	return std::rotr(x, 6) ^ std::rotr(x, 11) ^ std::rotr(x, 25);
}

[[nodiscard]] constexpr std::uint32_t smallSigma0(std::uint32_t x) {
	return std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3U);
}

[[nodiscard]] constexpr std::uint32_t smallSigma1(std::uint32_t x) {
	return std::rotr(x, 17) ^ std::rotr(x, 19) ^ (x >> 10U);
}

[[nodiscard]] std::uint32_t wordAt(std::span<const std::byte> block, std::size_t index) {
	const auto raw = block.subspan(index * kWordBytes, kWordBytes);
	return fromBigEndian<std::uint32_t>(std::span<const std::byte, kWordBytes>{raw});
}

[[nodiscard]] Schedule blockWords(std::span<const std::byte, kSha256BlockBytes> block) {
	Schedule schedule{};
	for (std::size_t at = 0; at < kBlockWords; ++at) {
		schedule.at(at) = wordAt(block, at);
	}
	return schedule;
}

// The remaining 48 words, derived from the first 16 (FIPS 180-4 §6.2.2 step 1).
void expand(Schedule& schedule) {
	for (std::size_t at = kBlockWords; at < kRounds; ++at) {
		schedule.at(at) = smallSigma1(schedule.at(at - 2)) + schedule.at(at - 7) +
						  smallSigma0(schedule.at(at - 15)) + schedule.at(at - 16);
	}
}

[[nodiscard]] Schedule scheduleOf(std::span<const std::byte, kSha256BlockBytes> block) {
	Schedule schedule = blockWords(block);
	expand(schedule);
	return schedule;
}

[[nodiscard]] Working round(Working v, const Schedule& schedule, std::size_t at) {
	const std::uint32_t choice = (v.e & v.f) ^ (~v.e & v.g);
	const std::uint32_t majority = (v.a & v.b) ^ (v.a & v.c) ^ (v.b & v.c);
	const std::uint32_t first =
		v.h + bigSigma1(v.e) + choice + kRoundConstants.at(at) + schedule.at(at);
	const std::uint32_t second = bigSigma0(v.a) + majority;
	return Working{
		.a = first + second,
		.b = v.a,
		.c = v.b,
		.d = v.c,
		.e = v.d + first,
		.f = v.e,
		.g = v.f,
		.h = v.g};
}

[[nodiscard]] Working workingFrom(const State& state) {
	return Working{
		.a = state.at(0),
		.b = state.at(1),
		.c = state.at(2),
		.d = state.at(3),
		.e = state.at(4),
		.f = state.at(5),
		.g = state.at(6),
		.h = state.at(7)};
}

[[nodiscard]] Working compress(Working start, const Schedule& schedule) {
	Working current = start;
	for (std::size_t at = 0; at < kRounds; ++at) {
		current = round(current, schedule, at);
	}
	return current;
}

void addInto(State& state, const Working& v) {
	state.at(0) += v.a;
	state.at(1) += v.b;
	state.at(2) += v.c;
	state.at(3) += v.d;
	state.at(4) += v.e;
	state.at(5) += v.f;
	state.at(6) += v.g;
	state.at(7) += v.h;
}

} // namespace

void compressSha256Block(State& state, std::span<const std::byte, kSha256BlockBytes> block) {
	addInto(state, compress(workingFrom(state), scheduleOf(block)));
}

} // namespace revenant::detail

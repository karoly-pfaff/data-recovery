// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace revenant {

inline constexpr std::size_t kSha256Bytes = 32;
inline constexpr std::size_t kSha256BlockBytes = 64;
inline constexpr std::size_t kSha256StateWords = 8;

// A SHA-256 digest, held as a value so artifacts can be compared and indexed by
// their content.
struct Sha256Digest {
	std::array<std::byte, kSha256Bytes> bytes;

	friend bool operator==(const Sha256Digest&, const Sha256Digest&) = default;
	friend auto operator<=>(const Sha256Digest&, const Sha256Digest&) = default;
};

// Streaming SHA-256 (FIPS 180-4). Fed whatever chunks the caller already has, so
// hashing a recovered file costs no extra pass over its bytes — the sink digests
// each artifact as it copies it.
class Sha256 {
public:
	void update(std::span<const std::byte> data);

	// The digest of everything fed so far. The state is spent afterwards.
	[[nodiscard]] Sha256Digest finish();

private:
	// One whole block into the state.
	void absorb(std::span<const std::byte, kSha256BlockBytes> block);

	// As much of `data` as the pending buffer needs to reach a whole block;
	// returns what is left over.
	[[nodiscard]] std::span<const std::byte> fillPending(std::span<const std::byte> data);

	// One padding byte into the pending block, absorbed as soon as the block is
	// whole. Deliberately not `update`: padding is not message content and must
	// not change the length it is padding to.
	void appendPadByte(std::byte value);

	// The 1 bit and the zeros that carry the block up to its length field.
	void padToLengthField();

	// The message length, padded and absorbed — the last thing a digest sees.
	void absorbTail();

	std::array<std::uint32_t, kSha256StateWords> state_{
		0x6a09e667U,
		0xbb67ae85U,
		0x3c6ef372U,
		0xa54ff53aU,
		0x510e527fU,
		0x9b05688cU,
		0x1f83d9abU,
		0x5be0cd19U};
	std::array<std::byte, kSha256BlockBytes> pending_{};
	std::size_t pendingBytes_ = 0;
	std::uint64_t totalBytes_ = 0;
};

[[nodiscard]] Sha256Digest sha256(std::span<const std::byte> data);

// The digest as 64 lowercase hex characters — how it appears in a manifest.
[[nodiscard]] std::string toHex(const Sha256Digest& digest);

} // namespace revenant

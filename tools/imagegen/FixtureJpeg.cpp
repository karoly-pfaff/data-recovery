// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/FixtureJpeg.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Endian.hpp"

namespace revenant::imagegen {

namespace {

// The frame — SOI, a 6-byte APP0, a 4-byte SOS and EOI — is `kJpegFrameBytes`
// in the header, because a caller has to know what is too short to stamp.
// Everything between is entropy-coded payload.
constexpr std::size_t kJpegHeaderBytes = 12;  // where the entropy run starts
constexpr std::size_t kEntropyModulus = 0xFE; // never produces a raw 0xFF

void appendEntropy(std::vector<std::byte>& jpeg, std::size_t count) {
	for (std::size_t i = 0; i < count; ++i) {
		jpeg.push_back(static_cast<std::byte>(i % kEntropyModulus));
	}
}

} // namespace

std::vector<std::byte> fixtureJpeg(std::size_t sizeBytes) {
	std::vector<std::byte> jpeg{
		std::byte{0xFF},
		std::byte{0xD8}, // SOI
		std::byte{0xFF},
		std::byte{0xE0},
		std::byte{0x00},
		std::byte{0x04}, // APP0, length 4
		std::byte{0x4A},
		std::byte{0x46}, // "JF"
		std::byte{0xFF},
		std::byte{0xDA},
		std::byte{0x00},
		std::byte{0x02}}; // SOS, length 2
	appendEntropy(jpeg, sizeBytes - kJpegFrameBytes);
	jpeg.push_back(std::byte{0xFF});
	jpeg.push_back(std::byte{0xD9}); // EOI
	return jpeg;
}

void stampJpegPayload(std::span<std::byte> jpeg, std::uint64_t token) {
	if (jpeg.size() < kJpegFrameBytes) {
		return;
	}
	const auto stamp = toLittleEndian<std::uint64_t>(token);
	const auto room = std::min(stamp.size(), jpeg.size() - kJpegFrameBytes);
	std::ranges::transform(
		std::span{stamp}.first(room),
		jpeg.subspan(kJpegHeaderBytes, room).begin(),
		[](std::byte value) {
			return static_cast<std::byte>(std::to_integer<unsigned>(value) % kEntropyModulus);
		});
}

} // namespace revenant::imagegen

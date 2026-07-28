// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/Sha256.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "core/Sha256Block.hpp"
#include "revenant/core/Endian.hpp"

namespace revenant {

namespace {

constexpr std::size_t kLengthBytes = 8;
constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
constexpr std::uint64_t kBitsPerByte = 8;
constexpr std::string_view kHexDigits = "0123456789abcdef";
constexpr unsigned int kHighNibbleShift = 4;
constexpr std::size_t kLowNibbleMask = 0x0F;

[[nodiscard]] Sha256Digest digestOf(const std::array<std::uint32_t, kSha256StateWords>& state) {
	Sha256Digest digest{};
	const std::span<std::byte> bytes{digest.bytes};
	for (std::size_t at = 0; at < state.size(); ++at) {
		std::ranges::copy(toBigEndian(state.at(at)), bytes.subspan(at * kWordBytes).begin());
	}
	return digest;
}

} // namespace

void Sha256::absorb(std::span<const std::byte, kSha256BlockBytes> block) {
	detail::compressSha256Block(state_, block);
}

std::span<const std::byte> Sha256::fillPending(std::span<const std::byte> data) {
	const std::span<std::byte> room = std::span<std::byte>{pending_}.subspan(pendingBytes_);
	const auto taken = std::min(room.size(), data.size());
	std::ranges::copy(data.first(taken), room.begin());
	pendingBytes_ += taken;
	return data.subspan(taken);
}

void Sha256::update(std::span<const std::byte> data) {
	totalBytes_ += data.size();
	std::span<const std::byte> rest = data;
	while (!rest.empty()) {
		rest = fillPending(rest);
		if (pendingBytes_ == kSha256BlockBytes) {
			absorb(pending_);
			pendingBytes_ = 0;
		}
	}
}

void Sha256::appendPadByte(std::byte value) {
	pending_.at(pendingBytes_) = value;
	++pendingBytes_;
	if (pendingBytes_ == kSha256BlockBytes) {
		absorb(pending_);
		pendingBytes_ = 0;
	}
}

void Sha256::padToLengthField() {
	appendPadByte(std::byte{0x80});
	while (pendingBytes_ != kSha256BlockBytes - kLengthBytes) {
		appendPadByte(std::byte{0});
	}
}

void Sha256::absorbTail() {
	const auto length = toBigEndian(totalBytes_ * kBitsPerByte);
	padToLengthField();
	for (const std::byte value : length) {
		appendPadByte(value);
	}
}

Sha256Digest Sha256::finish() {
	absorbTail();
	return digestOf(state_);
}

Sha256Digest sha256(std::span<const std::byte> data) {
	Sha256 hash;
	hash.update(data);
	return hash.finish();
}

std::string toHex(const Sha256Digest& digest) {
	std::string hex;
	hex.reserve(kSha256Bytes * 2);
	for (const std::byte value : digest.bytes) {
		const auto number = std::to_integer<std::size_t>(value);
		hex.push_back(kHexDigits.at(number >> kHighNibbleShift));
		hex.push_back(kHexDigits.at(number & kLowNibbleMask));
	}
	return hex;
}

} // namespace revenant

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/carve/CarveResult.hpp"
#include "revenant/carve/FormatCarver.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::testing {

// Test double: matches a fixed 2-byte magic and returns a configured verdict
// with a configured length, so scanner behavior is testable without a real
// format parser.
class FakeCarver final : public carve::FormatCarver {
public:
	FakeCarver(Confidence confidence, std::uint64_t length)
		: confidence_(confidence), length_(length) {}

	[[nodiscard]] std::span<const carve::Signature> signatures() const override {
		return {&signature_, 1};
	}

	[[nodiscard]] Result<carve::CarveResult> carve(ByteReader& reader) const override {
		const auto bounded = std::min<std::uint64_t>(length_, reader.size());
		return carve::CarveResult{
			.length = bounded,
			.confidence = confidence_,
			.extension = "fake"};
	}

private:
	static constexpr std::array<std::byte, 2> kMagic{std::byte{0xAB}, std::byte{0xCD}};
	carve::Signature signature_{.magic = kMagic, .offset = 0};
	Confidence confidence_;
	std::uint64_t length_;
};

} // namespace revenant::testing

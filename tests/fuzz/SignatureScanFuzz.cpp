// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: any device contents produce a clean scan (candidates or none) —
// never a crash, hang, or OOB. The FakeCarver-style inline carver returns a
// length derived from the input so resume arithmetic is exercised too.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "revenant/carve/CandidateVisitor.hpp"
#include "revenant/carve/CarveResult.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/FormatCarver.hpp"
#include "revenant/carve/ScanCandidate.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Result.hpp"
#include "support/InMemoryDevice.hpp"

namespace {

// Copies the fuzzer-owned input into `std::byte` storage we can safely hold
// past this call — via span iterators, never raw pointer arithmetic.
std::vector<std::byte> toByteVector(std::span<const std::uint8_t> input) {
	std::vector<std::byte> bytes(input.size());
	std::ranges::transform(input, bytes.begin(), [](std::uint8_t b) { return std::byte{b}; });
	return bytes;
}

class NullVisitor final : public revenant::carve::CandidateVisitor {
public:
	void onCandidate(const revenant::carve::ScanCandidate& candidate) override {
		static_cast<void>(candidate);
	}
};

class EchoCarver final : public revenant::carve::FormatCarver {
public:
	[[nodiscard]] std::span<const revenant::carve::Signature> signatures() const override {
		return {&signature_, 1};
	}

	[[nodiscard]] revenant::Result<revenant::carve::CarveResult>
	carve(revenant::ByteReader& reader) const override {
		const auto len = reader.readLe<std::uint16_t>(2);
		const std::uint64_t length = len.hasValue() ? len.value() : reader.size();
		return revenant::carve::CarveResult{
			.length = std::min<std::uint64_t>(length, reader.size()),
			.confidence = revenant::Confidence::kUncertain,
			.extension = "fzz"};
	}

private:
	static constexpr std::array<std::byte, 2> kMagic{std::byte{0xAB}, std::byte{0xCD}};
	revenant::carve::Signature signature_{.magic = kMagic, .offset = 0};
};

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	auto bytes = toByteVector(std::span<const std::uint8_t>{data, size});
	revenant::testing::InMemoryDevice device{std::move(bytes), 512};
	revenant::carve::CarverRegistry registry;
	registry.registerCarver(std::make_unique<EchoCarver>());
	const revenant::carve::ScanConfig config{.windowBytes = 64, .maxCarveBytes = 256};
	NullVisitor visitor;
	static_cast<void>(revenant::carve::SignatureScanner{registry, config}.scan(device, visitor));
	return 0;
}

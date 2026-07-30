// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "carve/CpuFeatures.hpp"
#include "carve/PrefilterAvx2.hpp"
#include "revenant/carve/BuiltinCarvers.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/SignatureTable.hpp"

namespace {

using revenant::carve::CarverRegistry;
using revenant::carve::kPrefilterChunkBytes;
using revenant::carve::kPrefilterVectorBytes;
using revenant::carve::MatchPath;
using revenant::carve::NibbleFilter;
using revenant::carve::registerBuiltinCarvers;
using revenant::carve::SurvivorMasks;
using revenant::carve::survivorsAvx2;

[[nodiscard]] CarverRegistry builtins() {
	CarverRegistry registry{MatchPath::kAuto};
	registerBuiltinCarvers(registry);
	return registry;
}

// One call answers for several vectors, and a chunk position belongs to exactly
// one of them: which mask holds it, and which bit of that mask it is.
[[nodiscard]] std::uint32_t bitAt(std::size_t at) {
	return std::uint32_t{1} << (at % kPrefilterVectorBytes);
}

[[nodiscard]] std::uint32_t maskFor(const SurvivorMasks& masks, std::size_t at) {
	return masks.at(at / kPrefilterVectorBytes);
}

[[nodiscard]] bool keeps(const SurvivorMasks& masks, std::size_t at) {
	return (maskFor(masks, at) & bitAt(at)) != 0;
}

class Prefilter : public testing::Test {
protected:
	void SetUp() override {
		if (!revenant::carve::buildHasAvx2() || !revenant::carve::cpuHasAvx2()) {
			GTEST_SKIP() << "no vectorized reject in this build or on this CPU";
		}
	}

	// A chunk of bytes nothing can start with, so a planted byte is the only
	// survivor and the masks say exactly where it was.
	[[nodiscard]] static std::vector<std::byte> quietChunk() {
		return std::vector<std::byte>(kPrefilterChunkBytes, std::byte{0x00});
	}

	[[nodiscard]] const NibbleFilter& filter() const {
		return registry_.signatureTable().nibbleFilter();
	}

	// Every position the byte-wise table keeps in `chunk` is a position the
	// vectorized reject also keeps. The other direction is allowed to differ.
	void expectKeepsEveryPositionTheTableKeeps(std::span<const std::byte> chunk) {
		const auto dropped = droppedPositionsIn(chunk);
		EXPECT_TRUE(dropped.empty()) << dropped.size() << " kept positions were dropped";
	}

	// Where the two disagree in the one direction that would be a defect.
	[[nodiscard]] std::vector<std::size_t>
	droppedPositionsIn(std::span<const std::byte> chunk) const {
		const auto masks = survivorsAvx2(chunk, filter());
		std::vector<std::size_t> dropped;
		for (const std::size_t kept : tableKeeps(chunk)) {
			if (!keeps(masks, kept)) {
				dropped.push_back(kept);
			}
		}
		return dropped;
	}

	// Every position the byte-wise table would keep. The answer the vector code
	// has to cover: it may cover more, and may never cover less.
	[[nodiscard]] std::vector<std::size_t> tableKeeps(std::span<const std::byte> chunk) const {
		std::vector<std::size_t> kept;
		std::size_t at = 0;
		for (const std::byte value : chunk) {
			if (!registry_.signatureTable().none(value)) {
				kept.push_back(at);
			}
			++at;
		}
		return kept;
	}

private:
	CarverRegistry registry_{builtins()};
};

TEST_F(Prefilter, LetsNothingThroughAChunkNoSignatureStartsIn) {
	const auto masks = survivorsAvx2(quietChunk(), filter());
	EXPECT_EQ(std::ranges::count(masks, 0U), static_cast<std::ptrdiff_t>(masks.size()));
}

TEST_F(Prefilter, FindsAMagicsFirstByteAtTheStartOfTheChunk) {
	auto chunk = quietChunk();
	chunk.front() = std::byte{0xFF};
	EXPECT_TRUE(keeps(survivorsAvx2(chunk, filter()), 0));
}

// The last position of the last vector is the one an off-by-one loses.
TEST_F(Prefilter, FindsAMagicsFirstByteInTheFinalPositionOfTheChunk) {
	auto chunk = quietChunk();
	chunk.back() = std::byte{0xFF};
	EXPECT_TRUE(keeps(survivorsAvx2(chunk, filter()), kPrefilterChunkBytes - 1));
}

// One planted byte in each vector of the batch, so no vector's mask is skipped.
TEST_F(Prefilter, ReportsEveryPositionAMagicCouldStartAtAcrossTheWholeBatch) {
	auto chunk = quietChunk();
	const std::array<std::size_t, 4> planted{3, 40, 80, 127};
	for (const std::size_t at : planted) {
		chunk.at(at) = std::byte{0xFF};
	}
	const auto masks = survivorsAvx2(chunk, filter());
	for (const std::size_t at : planted) {
		EXPECT_TRUE(keeps(masks, at)) << "position " << at << " was dropped";
	}
}

// The one direction that matters: the filter may pass a byte the table would
// reject, and must never reject one the table would pass. Checked over every
// byte value.
TEST_F(Prefilter, NeverDropsAPositionTheTableWouldKeep) {
	for (std::size_t value = 0; value < 256U; ++value) {
		auto chunk = quietChunk();
		chunk.at(value % kPrefilterChunkBytes) = static_cast<std::byte>(value);
		expectKeepsEveryPositionTheTableKeeps(chunk);
	}
}

// The scalar spelling of the same filter, which the vector code has to match
// exactly — not merely conservatively, since they are one definition.
TEST_F(Prefilter, TheScalarSpellingOfTheFilterAgreesWithTheVectorOne) {
	auto chunk = quietChunk();
	for (std::size_t value = 0; value < 256U; ++value) {
		const auto at = value % kPrefilterChunkBytes;
		chunk.at(at) = static_cast<std::byte>(value);
		const auto masks = survivorsAvx2(chunk, filter());
		EXPECT_EQ(keeps(masks, at), filter().passes(static_cast<std::byte>(value)));
		chunk.at(at) = std::byte{0x00};
	}
}

} // namespace

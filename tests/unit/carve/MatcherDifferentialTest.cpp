// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <span>
#include <string_view>
#include <vector>

#include "carve/WindowMatch.hpp"
#include "revenant/carve/BuiltinCarvers.hpp"
#include "revenant/carve/CarveResult.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/FormatCarver.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/carve/SignatureTable.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Result.hpp"
#include "support/ReferenceMatcher.hpp"

namespace {

using revenant::carve::CarverRegistry;
using revenant::carve::Match;
using revenant::carve::registerBuiltinCarvers;
using revenant::testing::referenceMatches;

// Fixed and printed on failure, so a red run is reproducible rather than a
// story about a window nobody kept.
constexpr std::uint32_t kSeed = 0x5EEDU;

// A constant seed is the point of this test, not an oversight: a differential
// failure that cannot be replayed is a bug report nobody can act on. The check
// guards code where unpredictability matters, which a matcher's input is not.
// NOLINTBEGIN(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed)
[[nodiscard]] std::mt19937 seededSource() {
	return std::mt19937{kSeed};
}

// NOLINTEND(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed)

// A magic that overlaps itself, which none of the registered formats' magics
// do: `AB AB AB` holds a match at position 0 and another at position 1. The
// matchers have to agree about that, and only a made-up format can ask them to.
class SelfOverlapCarver final : public revenant::carve::FormatCarver {
public:
	[[nodiscard]] std::span<const revenant::carve::Signature> signatures() const override {
		return {&signature_, 1};
	}

	[[nodiscard]] revenant::Result<revenant::carve::CarveResult>
	carve(revenant::ByteReader& reader) const override {
		static_cast<void>(reader);
		return revenant::carve::CarveResult{
			.length = 1,
			.confidence = revenant::Confidence::kRejected,
			.extension = "fake"};
	}

private:
	static constexpr std::array<std::byte, 2> kMagic{std::byte{0xAB}, std::byte{0xAB}};
	revenant::carve::Signature signature_{.magic = kMagic, .offset = 0};
};

[[nodiscard]] std::vector<std::byte> bytesOf(std::string_view text) {
	const auto view = std::as_bytes(std::span{text});
	return {view.begin(), view.end()};
}

void append(std::vector<std::byte>& into, std::string_view text) {
	const auto view = std::as_bytes(std::span{text});
	into.insert(into.end(), view.begin(), view.end());
}

// `ftyp` sits four bytes into an MP4, behind the box's own 32-bit length —
// which is why a hit is not a candidate start. `lead` is those four bytes.
[[nodiscard]] std::vector<std::byte> mp4Head(std::vector<std::byte> lead) {
	append(lead, "ftypisom");
	return lead;
}

[[nodiscard]] std::vector<Match>
onePass(std::span<const std::byte> window, std::uint64_t offset, const CarverRegistry& registry) {
	std::vector<Match> matches;
	revenant::carve::matchWindow(window, offset, registry.signatureTable(), matches);
	return matches;
}

// A match without the carver's address in it. Two registries hold two sets of
// carver objects, so their `Match::carver` pointers can never be equal even
// when they mean the same format; the registration index does mean the same
// thing in both, and `SignatureTableTest` pins that it tracks the carver.
struct Hit {
	std::uint64_t offset = 0;
	std::uint32_t carverIndex = 0;

	friend bool operator==(const Hit&, const Hit&) = default;
};

[[nodiscard]] std::vector<Hit> hitsOf(const std::vector<Match>& matches) {
	std::vector<Hit> hits;
	hits.reserve(matches.size());
	for (const Match& match : matches) {
		hits.push_back(Hit{.offset = match.offset, .carverIndex = match.carverIndex});
	}
	return hits;
}

// Every shipped carver, matched the way `path` asks for.
[[nodiscard]] CarverRegistry builtinsMatchedBy(revenant::carve::MatchPath path) {
	CarverRegistry registry{path};
	registerBuiltinCarvers(registry);
	return registry;
}

// Bytes drawn from a small alphabet. Uniform random bytes almost never contain
// a magic; an alphabet made of the bytes the registered signatures are built
// from produces hits, near-hits and boundary cases constantly, which is where
// two matchers disagree if they are going to.
[[nodiscard]] std::vector<std::byte> hostileWindow(std::mt19937& source, std::size_t size) {
	constexpr std::array<std::byte, 12> kAlphabet{
		std::byte{0xFF},
		std::byte{0xD8},
		std::byte{0x89},
		std::byte{0x50},
		std::byte{0x4E},
		std::byte{0x47},
		std::byte{0x4B},
		std::byte{0x25},
		std::byte{0x49},
		std::byte{0x2A},
		std::byte{0x66},
		std::byte{0x74}};
	std::uniform_int_distribution<std::size_t> pick{0, kAlphabet.size() - 1};
	std::vector<std::byte> window;
	window.reserve(size);
	for (std::size_t at = 0; at < size; ++at) {
		window.push_back(kAlphabet.at(pick(source)));
	}
	return window;
}

class MatcherDifferential : public testing::Test {
protected:
	// Three implementations, one answer. The reference is the oracle
	// story-0502 kept; the portable path is what every machine runs; the fast
	// path is what this machine runs if it can, and where it cannot the third
	// comparison repeats the second rather than being quietly absent.
	void expectAgreement(std::span<const std::byte> window, std::uint64_t offset) {
		const auto expected = referenceMatches(window, offset, registry_);
		expectDefaultPathAgrees(window, offset, expected);
		expectPortablePathAgrees(window, offset, expected);
	}

	// The oracle and whatever path this machine takes share a registry, so this
	// compares whole matches, carver identity included.
	void expectDefaultPathAgrees(
		std::span<const std::byte> window,
		std::uint64_t offset,
		const std::vector<Match>& expected) {
		EXPECT_EQ(onePass(window, offset, registry_), expected)
			<< "default path, offset " << offset << ", seed " << kSeed;
	}

	void expectPortablePathAgrees(
		std::span<const std::byte> window,
		std::uint64_t offset,
		const std::vector<Match>& expected) {
		EXPECT_EQ(hitsOf(onePass(window, offset, portable_)), hitsOf(expected))
			<< "portable path, offset " << offset << ", seed " << kSeed;
	}

	[[nodiscard]] const CarverRegistry& registry() const {
		return registry_;
	}

	[[nodiscard]] const CarverRegistry& portable() const {
		return portable_;
	}

private:
	CarverRegistry registry_{builtinsMatchedBy(revenant::carve::MatchPath::kAuto)};
	CarverRegistry portable_{builtinsMatchedBy(revenant::carve::MatchPath::kPortableOnly)};
};

// A test that quietly passes because it never executed is worse than no test,
// so the fast path says out loud whether it ran.
TEST_F(MatcherDifferential, SaysWhetherTheFastPathWasExercised) {
	if (registry().signatureTable().usesFastPath()) {
		EXPECT_TRUE(registry().signatureTable().usesFastPath());
		return;
	}
	GTEST_SKIP() << "this build or this CPU has no vectorized reject; the fast path"
					" comparisons below repeated the portable one";
}

TEST_F(MatcherDifferential, TheTwoPathsDisagreeAboutNothingTheTableKnows) {
	EXPECT_FALSE(portable().signatureTable().usesFastPath());
	EXPECT_EQ(portable().signatureTable().size(), registry().signatureTable().size());
}

TEST_F(MatcherDifferential, AgreesOnRandomizedWindows) {
	auto source = seededSource();
	for (int round = 0; round < 200; ++round) {
		expectAgreement(hostileWindow(source, 512), 0);
	}
}

// The same bytes at a device offset far from zero: every candidate start is
// then an offset the reference computes the same way, and none can underflow.
TEST_F(MatcherDifferential, AgreesOnAWindowFarFromTheDeviceStart) {
	auto source = seededSource();
	expectAgreement(hostileWindow(source, 4096), 1U << 30U);
}

// A magic at a non-zero Signature::offset — MP4's `ftyp` sits at 4 — is the
// reason a hit is not a candidate start.
TEST_F(MatcherDifferential, AgreesOnAMagicAtANonZeroInFileOffset) {
	expectAgreement(mp4Head(std::vector<std::byte>(4, std::byte{0})), 4096);
}

// Two carvers reporting a candidate at the same byte: a JPEG opening the window
// (its magic is `FF D8 FF` — SOI and the marker behind it) and an MP4 whose
// `ftyp` sits four bytes in both start a file at offset 0. Their order is the
// registration order, and both matchers must say so.
TEST_F(MatcherDifferential, AgreesWhenTwoCarversClaimTheSameCandidateOffset) {
	const auto window = mp4Head(bytesOf("\xFF\xD8\xFF\x2E"));
	expectAgreement(window, 0);
	EXPECT_EQ(onePass(window, 0, registry()).size(), 2U);
}

// A hit whose implied candidate start would underflow yields no match: `ftyp`
// four bytes into the device means a file starting at offset 0, which is fine —
// at the very first byte it means a file starting before the disk, which is not.
TEST_F(MatcherDifferential, AgreesWhenACandidateStartWouldUnderflow) {
	const auto window = bytesOf("ftypisom");
	expectAgreement(window, 0);
	EXPECT_TRUE(onePass(window, 0, registry()).empty());
}

TEST_F(MatcherDifferential, AgreesOnAMagicInTheWindowsFirstAndLastBytes) {
	const auto window = bytesOf("\xFF\xD8\xFF..\xFF\xD8\xFF");
	expectAgreement(window, 64);
	EXPECT_EQ(onePass(window, 64, registry()).size(), 2U);
}

TEST_F(MatcherDifferential, AgreesOnAnEmptyWindow) {
	expectAgreement({}, 0);
}

class OverlappingDifferential : public testing::Test {
protected:
	void expectAgreement(std::span<const std::byte> window) {
		EXPECT_EQ(onePass(window, 0, registry_), referenceMatches(window, 0, registry_));
	}

	[[nodiscard]] const CarverRegistry& registry() const {
		return registry_;
	}

private:
	[[nodiscard]] static CarverRegistry madeOfOverlap() {
		CarverRegistry registry;
		registry.registerCarver(std::make_unique<SelfOverlapCarver>());
		return registry;
	}

	CarverRegistry registry_{madeOfOverlap()};
};

TEST_F(OverlappingDifferential, AgreesOnOverlappingOccurrencesOfOneMagic) {
	const std::vector<std::byte> window(6, std::byte{0xAB});
	expectAgreement(window);
	EXPECT_EQ(onePass(window, 0, registry()).size(), 5U);
}

TEST_F(OverlappingDifferential, AgreesWhenTheMagicEndsOnTheWindowsLastByte) {
	expectAgreement(std::vector<std::byte>{std::byte{0x00}, std::byte{0xAB}, std::byte{0xAB}});
}

// A magic cut in half by the window's end is not a match: the scanner reads it
// whole in the next, overlapping window.
TEST_F(OverlappingDifferential, AgreesWhenTheMagicIsCutByTheWindowEnd) {
	const std::vector<std::byte> window{std::byte{0x00}, std::byte{0xAB}};
	expectAgreement(window);
	EXPECT_TRUE(onePass(window, 0, registry()).empty());
}

} // namespace

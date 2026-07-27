// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/carve/SignatureScanner.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "revenant/carve/CarveResult.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/FormatCarver.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Result.hpp"
#include "support/CollectingVisitor.hpp"
#include "support/FakeCarver.hpp"
#include "support/InMemoryDevice.hpp"

namespace {

using revenant::Confidence;
using revenant::Result;
using revenant::carve::CarverRegistry;
using revenant::carve::ScanConfig;
using revenant::carve::SignatureScanner;
using revenant::testing::CollectingVisitor;
using revenant::testing::FakeCarver;
using revenant::testing::InMemoryDevice;

constexpr std::uint32_t kSector = 512;

// Local to RejectedMatchAdvancesOneByte: that test needs two raw matches
// exactly one byte apart, genuinely overlapping. FakeCarver's magic
// ({0xAB, 0xCD}) can never do that — a non-palindromic 2-byte magic can't
// match at both position p and p+1 (the shared byte would have to equal
// both magic[0] and magic[1] at once). A self-overlapping magic
// ({0xAB, 0xAB}) can, so this double always reports Rejected/length-0 —
// the exact scenario the test is pinning.
class OverlappingRejectCarver final : public revenant::carve::FormatCarver {
public:
	[[nodiscard]] std::span<const revenant::carve::Signature> signatures() const override {
		return {&signature_, 1};
	}

	[[nodiscard]] Result<revenant::carve::CarveResult>
	carve(revenant::ByteReader& reader) const override {
		static_cast<void>(reader);
		return revenant::carve::CarveResult{
			.length = 0,
			.confidence = Confidence::kRejected,
			.extension = "fake"};
	}

private:
	static constexpr std::array<std::byte, 2> kMagic{std::byte{0xAB}, std::byte{0xAB}};
	revenant::carve::Signature signature_{.magic = kMagic, .offset = 0};
};

// A carver whose magic sits four bytes into its files (as MP4's `ftyp` does).
// The candidate therefore starts *before* the match, which is what makes the
// wrap-around case below reachable at all.
class OffsetSignatureCarver final : public revenant::carve::FormatCarver {
public:
	[[nodiscard]] std::span<const revenant::carve::Signature> signatures() const override {
		return {&signature_, 1};
	}

	[[nodiscard]] Result<revenant::carve::CarveResult>
	carve(revenant::ByteReader& reader) const override {
		static_cast<void>(reader);
		return revenant::carve::CarveResult{
			.length = 8,
			.confidence = Confidence::kValid,
			.extension = "offset"};
	}

private:
	static constexpr std::array<std::byte, 2> kMagic{std::byte{0xAB}, std::byte{0xCD}};
	revenant::carve::Signature signature_{.magic = kMagic, .offset = 4};
};

std::vector<std::byte> zeroes(std::size_t count) {
	return std::vector<std::byte>(count, std::byte{0});
}

void plantMagic(std::vector<std::byte>& bytes, std::size_t at) {
	bytes.at(at) = std::byte{0xAB};
	bytes.at(at + 1) = std::byte{0xCD};
}

CarverRegistry validRegistry(std::uint64_t carveLength) {
	CarverRegistry registry;
	registry.registerCarver(std::make_unique<FakeCarver>(Confidence::kValid, carveLength));
	return registry;
}

TEST(SignatureScanner, FindsMatchAtOffsetZeroAndMid) {
	auto bytes = zeroes(4096);
	plantMagic(bytes, 0);
	plantMagic(bytes, 1000);
	InMemoryDevice device{bytes, kSector};
	const auto registry = validRegistry(16);
	CollectingVisitor visitor;
	const auto stats = SignatureScanner{registry, ScanConfig{}}.scan(device, visitor);
	ASSERT_TRUE(stats.hasValue());
	ASSERT_EQ(visitor.candidates().size(), 2U);
	EXPECT_EQ(visitor.candidates().at(0).offset, 0U);
	EXPECT_EQ(visitor.candidates().at(1).offset, 1000U);
}

TEST(SignatureScanner, ResumesPastValidExtentWithoutRescanningIt) {
	auto bytes = zeroes(4096);
	plantMagic(bytes, 100);
	plantMagic(bytes, 108); // inside the previous candidate's 16-byte extent
	plantMagic(bytes, 200); // outside it
	InMemoryDevice device{bytes, kSector};
	const auto registry = validRegistry(16);
	CollectingVisitor visitor;
	ASSERT_TRUE((SignatureScanner{registry, ScanConfig{}}.scan(device, visitor).hasValue()));
	ASSERT_EQ(visitor.candidates().size(), 2U);
	EXPECT_EQ(visitor.candidates().at(0).offset, 100U);
	EXPECT_EQ(visitor.candidates().at(1).offset, 200U);
}

TEST(SignatureScanner, RejectedMatchAdvancesOneByte) {
	auto bytes = zeroes(1024);
	// Three consecutive 0xAB bytes: raw {0xAB, 0xAB} matches exist at both 50
	// and 51 (genuinely overlapping — they share byte 51). The match at 51 is
	// reachable only if a Rejected verdict resumes scanning at exactly +1
	// (52), not by any larger, length-derived skip that would jump past it.
	bytes.at(50) = std::byte{0xAB};
	bytes.at(51) = std::byte{0xAB};
	bytes.at(52) = std::byte{0xAB};
	InMemoryDevice device{bytes, kSector};
	CarverRegistry registry;
	registry.registerCarver(std::make_unique<OverlappingRejectCarver>());
	CollectingVisitor visitor;
	ASSERT_TRUE((SignatureScanner{registry, ScanConfig{}}.scan(device, visitor).hasValue()));
	ASSERT_EQ(visitor.candidates().size(), 2U);
	EXPECT_EQ(visitor.candidates().at(0).offset, 50U);
	EXPECT_EQ(visitor.candidates().at(1).offset, 51U);
}

TEST(SignatureScanner, FindsMatchStraddlingWindowBoundary) {
	ScanConfig config;
	config.windowBytes = 256; // force multiple windows
	auto bytes = zeroes(1024);
	plantMagic(bytes, 255); // second byte of the magic is in the next window
	InMemoryDevice device{bytes, kSector};
	const auto registry = validRegistry(8);
	CollectingVisitor visitor;
	ASSERT_TRUE((SignatureScanner{registry, config}.scan(device, visitor).hasValue()));
	ASSERT_EQ(visitor.candidates().size(), 1U);
	EXPECT_EQ(visitor.candidates().front().offset, 255U);
}

TEST(SignatureScanner, MatchAtDeviceEndYieldsBoundedCandidate) {
	auto bytes = zeroes(512);
	plantMagic(bytes, 510); // magic ends exactly at device end
	InMemoryDevice device{bytes, kSector};
	const auto registry = validRegistry(64); // wants 64, only 2 available
	CollectingVisitor visitor;
	ASSERT_TRUE((SignatureScanner{registry, ScanConfig{}}.scan(device, visitor).hasValue()));
	ASSERT_EQ(visitor.candidates().size(), 1U);
	EXPECT_EQ(visitor.candidates().front().result.length, 2U);
}

TEST(SignatureScanner, EmptyAndTinyDevicesProduceNoCandidates) {
	InMemoryDevice empty{zeroes(0), kSector};
	InMemoryDevice tiny{zeroes(1), kSector};
	const auto registry = validRegistry(8);
	CollectingVisitor visitor;
	ASSERT_TRUE((SignatureScanner{registry, ScanConfig{}}.scan(empty, visitor).hasValue()));
	ASSERT_TRUE((SignatureScanner{registry, ScanConfig{}}.scan(tiny, visitor).hasValue()));
	EXPECT_TRUE(visitor.candidates().empty());
}

TEST(SignatureScanner, ReportsBytesScanned) {
	InMemoryDevice device{zeroes(4096), kSector};
	const auto registry = validRegistry(8);
	CollectingVisitor visitor;
	const auto stats = SignatureScanner{registry, ScanConfig{}}.scan(device, visitor);
	ASSERT_TRUE(stats.hasValue());
	EXPECT_EQ(stats.value().bytesScanned, 4096U);
	EXPECT_EQ(stats.value().candidateCount, 0U);
}

TEST(SignatureScanner, AMagicNearerTheStartThanItsOwnOffsetIsNotACandidate) {
	auto bytes = zeroes(1024);
	plantMagic(bytes, 2);   // would start at -2: impossible, must be dropped
	plantMagic(bytes, 100); // starts at 96: a real candidate
	InMemoryDevice device{bytes, kSector};
	CarverRegistry registry;
	registry.registerCarver(std::make_unique<OffsetSignatureCarver>());
	CollectingVisitor visitor;
	ASSERT_TRUE((SignatureScanner{registry, ScanConfig{}}.scan(device, visitor).hasValue()));
	ASSERT_EQ(visitor.candidates().size(), 1U);
	EXPECT_EQ(visitor.candidates().front().offset, 96U);
}

} // namespace

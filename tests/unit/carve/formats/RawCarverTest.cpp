// SPDX-License-Identifier: GPL-3.0-or-later
#include "carve/formats/RawCarver.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"

namespace {

using revenant::ByteReader;
using revenant::Confidence;
using revenant::carve::RawCarver;

std::byte operator""_b(unsigned long long value) {
	return static_cast<std::byte>(value);
}

constexpr std::uint16_t kLongType = 4;
constexpr std::uint16_t kAsciiType = 2;
constexpr std::uint16_t kMakeTag = 0x010F;
constexpr std::uint16_t kStripOffsetsTag = 0x0111;
constexpr std::uint16_t kStripByteCountsTag = 0x0117;
constexpr std::uint16_t kTileOffsetsTag = 0x0144;
constexpr std::uint16_t kTileByteCountsTag = 0x0145;

// One IFD entry's fields, bundled so no two convertible integers sit adjacent
// in a parameter list.
struct EntrySpec {
	std::uint16_t tag = 0;
	std::uint16_t type = 0;
	std::uint32_t count = 0;
	std::uint32_t value = 0;
};

// A TIFF builder that writes in either byte order, so the same fixture proves
// the carver reads every field in the file's own order.
class TiffBuilder {
public:
	explicit TiffBuilder(bool bigEndian) : bigEndian_(bigEndian) {}

	void putU16(std::size_t at, std::uint16_t value) {
		grow(at + 2);
		bytes_.at(at + (bigEndian_ ? 1 : 0)) = static_cast<std::byte>(value & 0xFFU);
		bytes_.at(at + (bigEndian_ ? 0 : 1)) = static_cast<std::byte>(value >> 8U);
	}

	void putU32(std::size_t at, std::uint32_t value) {
		putU16(at + (bigEndian_ ? 2 : 0), static_cast<std::uint16_t>(value & 0xFFFFU));
		putU16(at + (bigEndian_ ? 0 : 2), static_cast<std::uint16_t>(value >> 16U));
	}

	void putBytes(std::size_t at, std::span<const std::byte> raw) {
		grow(at + raw.size());
		std::size_t cursor = at;
		for (const auto value : raw) {
			bytes_.at(cursor) = value;
			++cursor;
		}
	}

	void putAscii(std::size_t at, std::string_view text) {
		putBytes(at, std::as_bytes(std::span{text.data(), text.size()}));
	}

	// One 12-byte IFD entry at `at`.
	void putEntry(std::size_t at, const EntrySpec& spec) {
		putU16(at, spec.tag);
		putU16(at + 2, spec.type);
		putU32(at + 4, spec.count);
		putU32(at + 8, spec.value);
	}

	void putHeader(std::uint32_t firstIfd) {
		grow(8);
		bytes_.at(0) = bigEndian_ ? 0x4D_b : 0x49_b;
		bytes_.at(1) = bigEndian_ ? 0x4D_b : 0x49_b;
		putU16(2, 42);
		putU32(4, firstIfd);
	}

	[[nodiscard]] const std::vector<std::byte>& bytes() const {
		return bytes_;
	}

	void grow(std::size_t to) {
		if (bytes_.size() < to) {
			bytes_.resize(to, 0x00_b);
		}
	}

private:
	std::vector<std::byte> bytes_;
	bool bigEndian_;
};

// Header, one IFD of three entries (Make, StripOffsets, StripByteCounts), the
// Make string, then the image data. Extent = end of the image data.
constexpr std::size_t kIfdAt = 8;
constexpr std::size_t kIfdEnd = 8 + 2 + (3 * 12) + 4; // 50
constexpr std::size_t kMakeAt = kIfdEnd;
constexpr std::size_t kMakeBytes = 8;
constexpr std::size_t kDataAt = kMakeAt + kMakeBytes; // 58
constexpr std::size_t kDataBytes = 64;
constexpr std::size_t kFileEnd = kDataAt + kDataBytes; // 122

[[nodiscard]] std::vector<std::byte>
minimalTiff(bool bigEndian, std::string_view make, bool tiled = false) {
	TiffBuilder builder{bigEndian};
	builder.putHeader(kIfdAt);
	builder.putU16(kIfdAt, 3);
	builder.putEntry(
		kIfdAt + 2,
		EntrySpec{
			.tag = kMakeTag,
			.type = kAsciiType,
			.count = static_cast<std::uint32_t>(kMakeBytes),
			.value = static_cast<std::uint32_t>(kMakeAt)});
	builder.putEntry(
		kIfdAt + 14,
		EntrySpec{
			.tag = tiled ? kTileOffsetsTag : kStripOffsetsTag,
			.type = kLongType,
			.count = 1,
			.value = static_cast<std::uint32_t>(kDataAt)});
	builder.putEntry(
		kIfdAt + 26,
		EntrySpec{
			.tag = tiled ? kTileByteCountsTag : kStripByteCountsTag,
			.type = kLongType,
			.count = 1,
			.value = static_cast<std::uint32_t>(kDataBytes)});
	builder.putU32(kIfdAt + 38, 0); // end of chain
	builder.putAscii(kMakeAt, make);
	builder.grow(kFileEnd);
	return builder.bytes();
}

TEST(RawCarver, LittleEndianTiffYieldsExactLengthAndValid) {
	const auto bytes = minimalTiff(false, "NIKON");
	ByteReader reader{bytes};
	const auto result = RawCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().length, kFileEnd);
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
}

TEST(RawCarver, BigEndianTiffYieldsTheSameResult) {
	const auto bytes = minimalTiff(true, "NIKON");
	ByteReader reader{bytes};
	const auto result = RawCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().length, kFileEnd);
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
}

TEST(RawCarver, ExtentStopsAtTheImageDataDespiteTrailingGarbage) {
	auto bytes = minimalTiff(false, "NIKON");
	bytes.resize(bytes.size() + 500, 0xEE_b);
	ByteReader reader{bytes};
	const auto result = RawCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().length, kFileEnd); // THE anti-false-positive assertion
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
}

TEST(RawCarver, TileTagsLocateTheImageDataToo) {
	const auto bytes = minimalTiff(false, "Canon", true);
	ByteReader reader{bytes};
	const auto result = RawCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().length, kFileEnd);
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
}

TEST(RawCarver, TheMakeTagPicksTheExtension) {
	const auto nikon = minimalTiff(false, "NIKON");
	ByteReader nikonReader{nikon};
	EXPECT_EQ(RawCarver{}.carve(nikonReader).value().extension, "nef");

	const auto sony = minimalTiff(false, "SONY");
	ByteReader sonyReader{sony};
	EXPECT_EQ(RawCarver{}.carve(sonyReader).value().extension, "arw");

	const auto other = minimalTiff(false, "ACME");
	ByteReader otherReader{other};
	EXPECT_EQ(RawCarver{}.carve(otherReader).value().extension, "tif");
}

// A real CR2 carries Canon's marker at offset 8, so its first IFD lives
// further in — the marker and an IFD cannot share that address.
TEST(RawCarver, TheCanonHeaderMarkerPicksCr2) {
	constexpr std::size_t kCr2IfdAt = 16;
	TiffBuilder builder{false};
	builder.putHeader(kCr2IfdAt);
	builder.putAscii(8, "CR");
	builder.putU16(kCr2IfdAt, 2);
	builder.putEntry(
		kCr2IfdAt + 2,
		EntrySpec{.tag = kStripOffsetsTag, .type = kLongType, .count = 1, .value = 64});
	builder.putEntry(
		kCr2IfdAt + 14,
		EntrySpec{.tag = kStripByteCountsTag, .type = kLongType, .count = 1, .value = 32});
	builder.putU32(kCr2IfdAt + 26, 0);
	builder.grow(96);
	const auto bytes = builder.bytes();
	ByteReader reader{bytes};
	const auto result = RawCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().extension, "cr2");
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
	EXPECT_EQ(result.value().length, 96U);
}

TEST(RawCarver, ASelfReferencingIfdChainTerminates) {
	auto builder = TiffBuilder{false};
	builder.putHeader(kIfdAt);
	builder.putU16(kIfdAt, 1);
	builder.putEntry(
		kIfdAt + 2,
		EntrySpec{.tag = kStripOffsetsTag, .type = kLongType, .count = 1, .value = 40});
	builder.putU32(kIfdAt + 14, kIfdAt); // points back at itself
	builder.grow(128);
	const auto bytes = builder.bytes();
	ByteReader reader{bytes};
	const auto result = RawCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kUncertain);
}

TEST(RawCarver, AStripPointingPastTheDataIsUncertainAndBounded) {
	auto bytes = minimalTiff(false, "NIKON");
	// Point the strip offset far past the buffer.
	bytes.at(kIfdAt + 14 + 8) = 0xFF_b;
	bytes.at(kIfdAt + 14 + 9) = 0xFF_b;
	ByteReader reader{bytes};
	const auto result = RawCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kUncertain);
	EXPECT_LE(result.value().length, bytes.size());
}

TEST(RawCarver, AnIfdWithoutImageDataIsUncertain) {
	TiffBuilder builder{false};
	builder.putHeader(kIfdAt);
	builder.putU16(kIfdAt, 1);
	builder.putEntry(
		kIfdAt + 2,
		EntrySpec{.tag = kMakeTag, .type = kAsciiType, .count = 4, .value = 0x4E4F4E45});
	builder.putU32(kIfdAt + 14, 0);
	builder.grow(64);
	const auto bytes = builder.bytes();
	ByteReader reader{bytes};
	const auto result = RawCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kUncertain);
}

TEST(RawCarver, NonTiffBytesAreRejected) {
	const std::vector<std::byte> bytes(64, 0x5A_b);
	ByteReader reader{bytes};
	const auto result = RawCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kRejected);
	EXPECT_EQ(result.value().length, 0U);
}

TEST(RawCarver, EmptyInputIsRejected) {
	const std::vector<std::byte> bytes;
	ByteReader reader{bytes};
	const auto result = RawCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kRejected);
}

TEST(RawCarver, BothByteOrdersAreRegisteredSignatures) {
	const auto signatures = RawCarver{}.signatures();
	ASSERT_EQ(signatures.size(), 2U);
	EXPECT_EQ(signatures.front().offset, 0U);
	EXPECT_EQ(signatures.front().magic.size(), 4U);
}

} // namespace

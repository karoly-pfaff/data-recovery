// SPDX-License-Identifier: GPL-3.0-or-later
#include "carve/formats/PdfCarver.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"

namespace {

using revenant::ByteReader;
using revenant::Confidence;
using revenant::carve::PdfCarver;

std::byte operator""_b(unsigned long long value) {
	return static_cast<std::byte>(value);
}

[[nodiscard]] std::vector<std::byte> bytesOf(std::string_view text) {
	const auto raw = std::as_bytes(std::span{text.data(), text.size()});
	return std::vector<std::byte>{raw.begin(), raw.end()};
}

// A one-revision PDF: header, one object, an xref table, and a trailer whose
// startxref points at that table.
[[nodiscard]] std::string onePdfRevision() {
	std::string pdf = "%PDF-1.7\n";
	pdf += "1 0 obj\n<< /Type /Catalog >>\nendobj\n";
	const auto xrefAt = pdf.size();
	pdf += "xref\n0 2\n0000000000 65535 f \n";
	pdf += "trailer\n<< /Size 2 >>\nstartxref\n" + std::to_string(xrefAt) + "\n%%EOF";
	return pdf;
}

TEST(PdfCarver, ValidPdfYieldsExactLengthAndValid) {
	const auto bytes = bytesOf(onePdfRevision());
	ByteReader reader{bytes};
	const auto result = PdfCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().length, bytes.size());
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
	EXPECT_EQ(result.value().extension, "pdf");
}

TEST(PdfCarver, ExtentStopsAtTheEndMarkerDespiteTrailingGarbage) {
	auto bytes = bytesOf(onePdfRevision());
	const auto realSize = bytes.size();
	bytes.resize(realSize + 400, 0xEE_b);
	ByteReader reader{bytes};
	const auto result = PdfCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().length, realSize); // THE anti-false-positive assertion
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
}

// The case that makes "last, not first" matter: a PDF saved twice carries two
// end markers, and stopping at the first loses the newer revision entirely.
TEST(PdfCarver, AnIncrementallySavedPdfReachesItsSecondEndMarker) {
	auto pdf = onePdfRevision();
	pdf += "\n2 0 obj\n<< /Type /Page >>\nendobj\n";
	const auto secondXrefAt = pdf.size();
	pdf += "xref\n0 1\n0000000000 65535 f \n";
	pdf += "trailer\n<< /Size 3 >>\nstartxref\n" + std::to_string(secondXrefAt) + "\n%%EOF";
	const auto bytes = bytesOf(pdf);
	ByteReader reader{bytes};
	const auto result = PdfCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().length, bytes.size());
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
}

TEST(PdfCarver, TheLineEndingAfterTheMarkerBelongsToTheFile) {
	const auto bytes = bytesOf(onePdfRevision() + "\r\n");
	ByteReader reader{bytes};
	const auto result = PdfCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().length, bytes.size());
}

TEST(PdfCarver, ACrossReferenceStreamObjectAlsoResolves) {
	std::string pdf = "%PDF-1.7\n";
	const auto streamAt = pdf.size();
	pdf += "12 0 obj\n<< /Type /XRef >>\nendobj\n";
	pdf += "startxref\n" + std::to_string(streamAt) + "\n%%EOF";
	const auto bytes = bytesOf(pdf);
	ByteReader reader{bytes};
	const auto result = PdfCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kValid);
}

TEST(PdfCarver, AStartxrefPointingPastTheFileIsUncertainButStillExact) {
	std::string pdf = "%PDF-1.7\nxref\n0 1\n";
	pdf += "startxref\n999999\n%%EOF";
	const auto bytes = bytesOf(pdf);
	ByteReader reader{bytes};
	const auto result = PdfCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kUncertain);
	EXPECT_EQ(result.value().length, bytes.size());
}

TEST(PdfCarver, AnEndMarkerWithoutAStartxrefIsUncertain) {
	const auto bytes = bytesOf("%PDF-1.7\nsome text with no trailer at all\n%%EOF");
	ByteReader reader{bytes};
	const auto result = PdfCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kUncertain);
	EXPECT_EQ(result.value().length, bytes.size());
}

TEST(PdfCarver, AHeaderWithoutAnEndMarkerIsRejected) {
	const auto bytes = bytesOf("%PDF-1.7\n1 0 obj\n<< >>\nendobj\n");
	ByteReader reader{bytes};
	const auto result = PdfCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kRejected);
	EXPECT_EQ(result.value().length, 0U);
}

TEST(PdfCarver, NonPdfBytesAreRejected) {
	const std::vector<std::byte> bytes(64, 0x5A_b);
	ByteReader reader{bytes};
	const auto result = PdfCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kRejected);
}

TEST(PdfCarver, EmptyInputIsRejected) {
	const std::vector<std::byte> bytes;
	ByteReader reader{bytes};
	const auto result = PdfCarver{}.carve(reader);
	ASSERT_TRUE(result.hasValue());
	EXPECT_EQ(result.value().confidence, Confidence::kRejected);
}

TEST(PdfCarver, SignatureIsThePdfHeaderAtOffsetZero) {
	const auto signatures = PdfCarver{}.signatures();
	ASSERT_EQ(signatures.size(), 1U);
	EXPECT_EQ(signatures.front().offset, 0U);
	EXPECT_EQ(signatures.front().magic.size(), 5U);
}

} // namespace

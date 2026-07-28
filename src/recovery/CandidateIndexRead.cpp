// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "recovery/CandidateRecord.hpp"
#include "revenant/core/BoundedCount.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/recovery/Candidate.hpp"
#include "revenant/recovery/CandidateIndex.hpp"

namespace revenant::recovery {

namespace {

// A file open for reading, together with its length — the two travel together
// because every check below is against that length.
struct SizedFile {
	std::ifstream stream;
	std::uint64_t bytes;
};

[[nodiscard]] SizedFile openSized(const std::filesystem::path& path) {
	std::ifstream stream{path, std::ios::binary};
	stream.seekg(0, std::ios::end);
	const auto end = stream.tellg();
	stream.seekg(0, std::ios::beg);
	return SizedFile{
		.stream = std::move(stream),
		.bytes = end > 0 ? static_cast<std::uint64_t>(end) : 0};
}

// A byte range of one of the two index files.
struct FileRange {
	std::uint64_t offset;
	std::size_t length;
};

// The two files an index is: fixed records, and the blob they point into.
struct IndexFiles {
	SizedFile records;
	SizedFile blob;
};

[[nodiscard]] std::vector<std::byte> readAt(SizedFile& file, FileRange range) {
	std::vector<char> raw(range.length, '\0');
	file.stream.seekg(static_cast<std::streamoff>(range.offset), std::ios::beg);
	file.stream.read(raw.data(), static_cast<std::streamsize>(range.length));
	std::vector<std::byte> bytes(range.length, std::byte{0});
	std::ranges::transform(raw, bytes.begin(), [](char value) {
		return std::bit_cast<std::byte>(value);
	});
	return bytes;
}

[[nodiscard]] std::string toString(std::span<const std::byte> raw) {
	std::string text(raw.size(), '\0');
	std::ranges::transform(raw, text.begin(), [](std::byte value) {
		return std::bit_cast<char>(value);
	});
	return text;
}

// Every length a record states about itself, checked before any of them sizes
// an allocation or a seek (ADR-0009).
[[nodiscard]] bool lengthsAreSane(const CandidateRecord& record) {
	return boundedCount(record.nameLength, kMaxCandidateNameBytes).hasValue() &&
		   boundedCount(record.extentCount, kMaxCandidateExtents).hasValue() &&
		   boundedCount(record.residentLength, kMaxResidentBytes).hasValue();
}

[[nodiscard]] std::uint64_t entryBytes(const CandidateRecord& record) noexcept {
	return std::uint64_t{record.nameLength} + (std::uint64_t{record.extentCount} * kExtentBytes) +
		   std::uint64_t{record.residentLength};
}

// The blob range a record points at, when it is one the blob actually holds.
[[nodiscard]] bool blobRangeFits(const CandidateRecord& record, std::uint64_t blobBytes) noexcept {
	const auto needed = entryBytes(record);
	return record.blobOffset <= blobBytes && needed <= blobBytes - record.blobOffset;
}

[[nodiscard]] std::vector<fs::Extent>
decodeExtents(std::span<const std::byte> raw, std::uint32_t count) {
	const ByteReader reader{raw};
	std::vector<fs::Extent> extents;
	extents.reserve(count);
	for (std::uint32_t i = 0; i < count; ++i) {
		const auto at = std::size_t{i} * kExtentBytes;
		extents.push_back(
			fs::Extent{
				.deviceOffset = reader.readLe<std::uint64_t>(at).value(),
				.lengthBytes = reader.readLe<std::uint64_t>(at + 8).value()});
	}
	return extents;
}

[[nodiscard]] Candidate
decodeCandidate(const CandidateRecord& record, std::span<const std::byte> entry) {
	const auto name = entry.first(record.nameLength);
	const auto extents =
		entry.subspan(record.nameLength, std::size_t{record.extentCount} * kExtentBytes);
	const auto resident = entry.last(record.residentLength);
	return Candidate{
		.name = toString(name),
		.extents = decodeExtents(extents, record.extentCount),
		.residentContent = std::vector<std::byte>{resident.begin(), resident.end()},
		.timestamps =
			fs::Timestamps{
				.created = record.created,
				.modified = record.modified,
				.accessed = record.accessed},
		.confidence = static_cast<Confidence>(record.confidence),
		.source = static_cast<CandidateSource>(record.source)};
}

// Nothing when the record cannot be trusted: an impossible length, or a blob
// range the blob does not hold. Such a record is dropped and counted, never
// half-interpreted.
[[nodiscard]] std::optional<Candidate>
readCandidate(const CandidateRecord& record, SizedFile& blob) {
	if (!lengthsAreSane(record) || !blobRangeFits(record, blob.bytes)) {
		return std::nullopt;
	}
	const auto entry = readAt(
		blob,
		FileRange{
			.offset = record.blobOffset,
			.length = static_cast<std::size_t>(entryBytes(record))});
	return decodeCandidate(record, entry);
}

// How many whole records the index file holds, or a typed error when it is not
// one of ours or claims more than may be materialized.
[[nodiscard]] Result<std::size_t> recordCount(SizedFile& records) {
	const FileRange header{.offset = 0, .length = kIndexHeaderBytes};
	if (records.bytes < kIndexHeaderBytes || !headerIsOurs(readAt(records, header))) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = 0};
	}
	const auto area = records.bytes - kIndexHeaderBytes;
	return boundedCount(area / kRecordBytes, kMaxIndexedCandidates);
}

// A torn tail — an interrupted append — is bytes past the last whole record.
[[nodiscard]] std::uint64_t tornTailRecords(const SizedFile& records) noexcept {
	return (records.bytes - kIndexHeaderBytes) % kRecordBytes == 0 ? 0 : 1;
}

void appendCandidate(IndexContents& contents, std::optional<Candidate> candidate) {
	if (!candidate.has_value()) {
		++contents.droppedRecords;
		return;
	}
	contents.candidates.push_back(std::move(candidate.value()));
}

// Reads record number `ordinal` and folds whatever it yields into `contents`.
void readOneRecord(IndexContents& contents, IndexFiles& files, std::size_t ordinal) {
	const FileRange at{
		.offset = kIndexHeaderBytes + (ordinal * kRecordBytes),
		.length = kRecordBytes};
	const auto record = decodeRecord(readAt(files.records, at));
	appendCandidate(contents, readCandidate(record, files.blob));
}

[[nodiscard]] IndexContents readAll(IndexFiles& files, std::size_t count) {
	IndexContents contents{.candidates = {}, .droppedRecords = tornTailRecords(files.records)};
	for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
		readOneRecord(contents, files, ordinal);
	}
	return contents;
}

} // namespace

Result<IndexContents> readIndex(const std::filesystem::path& directory) {
	IndexFiles files{
		.records = openSized(directory / kIndexFileName),
		.blob = openSized(directory / kBlobFileName)};
	const auto count = recordCount(files.records);
	if (!count.hasValue()) {
		return count.error();
	}
	return readAll(files, count.value());
}

} // namespace revenant::recovery

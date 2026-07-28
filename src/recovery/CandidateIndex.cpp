// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/CandidateIndex.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include "recovery/CandidateRecord.hpp"
#include "revenant/core/Endian.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/recovery/Candidate.hpp"

namespace revenant::recovery {

namespace {

void appendBytes(std::vector<std::byte>& blob, std::span<const std::byte> raw) {
	blob.insert(blob.end(), raw.begin(), raw.end());
}

void appendExtent(std::vector<std::byte>& blob, const fs::Extent& extent) {
	appendBytes(blob, toLittleEndian<std::uint64_t>(extent.deviceOffset));
	appendBytes(blob, toLittleEndian<std::uint64_t>(extent.lengthBytes));
}

// One candidate's variable-length parts, in the order the record's three
// lengths describe them: name, extents, resident content.
[[nodiscard]] std::vector<std::byte> encodeBlobEntry(const Candidate& candidate) {
	std::vector<std::byte> blob;
	appendBytes(blob, std::as_bytes(std::span{candidate.name}));
	for (const fs::Extent& extent : candidate.extents) {
		appendExtent(blob, extent);
	}
	appendBytes(blob, candidate.residentContent);
	return blob;
}

[[nodiscard]] CandidateRecord
recordFor(const Candidate& candidate, std::uint64_t blobOffset) noexcept {
	return CandidateRecord{
		.blobOffset = blobOffset,
		.created = candidate.timestamps.created,
		.modified = candidate.timestamps.modified,
		.accessed = candidate.timestamps.accessed,
		.nameLength = static_cast<std::uint32_t>(candidate.name.size()),
		.residentLength = static_cast<std::uint32_t>(candidate.residentContent.size()),
		.extentCount = static_cast<std::uint32_t>(candidate.extents.size()),
		.confidence = static_cast<std::uint8_t>(candidate.confidence),
		.source = static_cast<std::uint8_t>(candidate.source)};
}

void write(std::ofstream& stream, std::span<const std::byte> raw) {
	for (const std::byte value : raw) {
		stream.put(std::bit_cast<char>(value));
	}
}

[[nodiscard]] std::ofstream openTruncating(const std::filesystem::path& path) {
	return std::ofstream{path, std::ios::binary | std::ios::trunc};
}

[[nodiscard]] std::ofstream openAppending(const std::filesystem::path& path) {
	return std::ofstream{path, std::ios::binary | std::ios::app};
}

// Sized by seeking rather than by `std::filesystem::file_size`: the MSVC
// implementation of the latter trips a clang-analyzer false positive inside its
// own header, which nothing here can annotate away.
[[nodiscard]] std::uintmax_t sizeOf(const std::filesystem::path& path) {
	std::ifstream stream{path, std::ios::binary | std::ios::ate};
	const auto end = stream.tellg();
	return end < 0 ? 0 : static_cast<std::uintmax_t>(end);
}

// Where a record file has to end to hold exactly `records` candidates.
[[nodiscard]] std::uintmax_t recordAreaBytes(std::uint64_t records) {
	return kIndexHeaderBytes + (static_cast<std::uintmax_t>(records) * kRecordBytes);
}

} // namespace

CandidateIndex::CandidateIndex(std::ofstream records, std::ofstream blob)
	: records_(std::move(records)), blob_(std::move(blob)) {}

Result<CandidateIndex> CandidateIndex::create(const std::filesystem::path& directory) {
	auto records = openTruncating(directory / kIndexFileName);
	auto blob = openTruncating(directory / kBlobFileName);
	write(records, encodeIndexHeader());
	records.flush();
	if (!records.good() || !blob.good()) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	return CandidateIndex{std::move(records), std::move(blob)};
}

Result<CandidateIndex>
CandidateIndex::continued(const std::filesystem::path& directory, const Continuation& from) {
	auto records = openAppending(directory / kIndexFileName);
	auto blob = openAppending(directory / kBlobFileName);
	if (!records.good() || !blob.good()) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	CandidateIndex index{std::move(records), std::move(blob)};
	index.blobBytes_ = from.blobBytes;
	index.count_ = from.records;
	return index;
}

Result<CandidateIndex>
CandidateIndex::reopen(const std::filesystem::path& directory, std::uint64_t records) {
	const auto wanted = recordAreaBytes(records);
	if (sizeOf(directory / kIndexFileName) < wanted) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	std::error_code failed;
	std::filesystem::resize_file(directory / kIndexFileName, wanted, failed);
	if (failed) {
		return Error{
			.code = ErrorCode::kIoFailure,
			.osCode = static_cast<std::int32_t>(failed.value())};
	}
	return continued(
		directory,
		Continuation{.records = records, .blobBytes = sizeOf(directory / kBlobFileName)});
}

Result<std::uint64_t> CandidateIndex::writeEntry(const Candidate& candidate) {
	const auto entry = encodeBlobEntry(candidate);
	write(blob_, entry);
	// The blob is flushed before the record that points at it, so no record can
	// ever refer to bytes that are not on disk.
	blob_.flush();
	write(records_, encodeRecord(recordFor(candidate, blobBytes_)));
	records_.flush();
	if (!blob_.good() || !records_.good()) {
		return Error{.code = ErrorCode::kIoFailure, .offset = blobBytes_};
	}
	blobBytes_ += entry.size();
	return count_++;
}

Result<std::uint64_t> CandidateIndex::append(const Candidate& candidate) {
	return writeEntry(candidate);
}

std::uint64_t CandidateIndex::count() const noexcept {
	return count_;
}

} // namespace revenant::recovery

// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ntfs/NtfsImageBuilder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <vector>

#include "imagegen/ntfs/AttributeBuilder.hpp"
#include "imagegen/ntfs/BootSectorBuilder.hpp"
#include "imagegen/ntfs/ByteWriter.hpp"
#include "imagegen/ntfs/FixtureFiles.hpp"
#include "imagegen/ntfs/MftRecordBuilder.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/ntfs/Runlist.hpp"

namespace revenant::imagegen::ntfs {

namespace {

using revenant::fs::ntfs::DataRun;

void append(std::vector<std::byte>& out, const std::vector<std::byte>& part) {
	out.insert(out.end(), part.begin(), part.end());
}

// A directory carries no `$DATA` at all; everything else carries exactly one,
// in the form the fixture assigned it.
void appendData(
	std::vector<std::byte>& out,
	const FixtureFile& file,
	const NtfsLayout& layout,
	std::uint64_t realSize) {
	if (file.dataKind == DataKind::kResident) {
		append(out, buildResidentData(file.content));
	}
	if (file.dataKind == DataKind::kNonResident) {
		append(
			out,
			buildNonResidentData(
				NonResidentDataSpec{
					.runs = file.runs,
					.realSize = realSize,
					.bytesPerCluster = layout.bytesPerCluster()}));
	}
}

// `$MFT`'s own record declares the MFT's byte size, which its content vector
// (empty — the MFT is not planted as file data) cannot supply.
[[nodiscard]] std::uint64_t declaredSize(const FixtureFile& file, const NtfsLayout& layout) {
	if (file.recordNumber != kMftRecord) {
		return file.content.size();
	}
	return std::uint64_t{layout.mftRecordCount} * layout.mftRecordBytes;
}

[[nodiscard]] std::vector<std::byte>
attributesFor(const FixtureFile& file, const NtfsLayout& layout) {
	const auto realSize = declaredSize(file, layout);
	std::vector<std::byte> out;
	append(out, buildStandardInformation());
	append(
		out,
		buildFileName(
			FileNameSpec{
				.parentRecord = file.parentRecord,
				.parentSequence = 1,
				.name = file.name,
				.realSize = realSize}));
	appendData(out, file, layout, realSize);
	append(out, buildEndMarker());
	return out;
}

void putRecord(std::vector<std::byte>& image, const FixtureFile& file, const NtfsLayout& layout) {
	const auto attributes = attributesFor(file, layout);
	const auto record = buildMftRecord(
		layout,
		MftRecordSpec{
			.sequence = 1,
			.inUse = file.inUse,
			.isDirectory = file.isDirectory,
			.attributes = attributes});
	const auto offset = layout.mftOffsetBytes() + (file.recordNumber * layout.mftRecordBytes);
	putBytes(image, static_cast<std::size_t>(offset), record);
}

// Lays `content` down across `runs` the way the filesystem would have: run by
// run, in file order, each run filled before the next begins.
void putAcrossRuns(
	std::vector<std::byte>& image,
	const NtfsLayout& layout,
	std::span<const DataRun> runs,
	std::span<const std::byte> content) {
	std::size_t taken = 0;
	for (const auto& run : runs) {
		const auto capacity =
			static_cast<std::size_t>(run.lengthClusters) * layout.bytesPerCluster();
		const auto chunk = std::min(capacity, content.size() - taken);
		putBytes(
			image,
			static_cast<std::size_t>(layout.clusterOffsetBytes(run.startCluster)),
			content.subspan(taken, chunk));
		taken += chunk;
	}
}

void putFileData(std::vector<std::byte>& image, const NtfsLayout& layout, const FixtureFile& file) {
	if (file.dataKind != DataKind::kNonResident || file.content.empty()) {
		return;
	}
	putAcrossRuns(image, layout, file.runs, file.content);
}

// A JPEG with no record pointing at it: what the filesystem pass cannot see and
// the carve pass must find.
void putUnallocatedJpeg(std::vector<std::byte>& image, const NtfsLayout& layout) {
	const auto jpeg = unallocatedJpeg();
	const auto offset = layout.clusterOffsetBytes(kUnallocatedJpegCluster);
	putBytes(image, static_cast<std::size_t>(offset), jpeg);
}

void putVolume(std::vector<std::byte>& image, const NtfsLayout& layout) {
	for (const auto& file : fixtureFiles(layout)) {
		putFileData(image, layout, file);
		putRecord(image, file, layout);
	}
	putUnallocatedJpeg(image, layout);
}

} // namespace

std::vector<std::byte> buildNtfsImage() {
	const auto layout = makeLayout();
	std::vector<std::byte> image(static_cast<std::size_t>(layout.totalBytes()), std::byte{0});
	putBytes(image, 0, buildBootSector(layout));
	putVolume(image, layout);
	return image;
}

Result<std::uint64_t> writeNtfsImage(const std::filesystem::path& path) {
	const auto image = buildNtfsImage();
	std::ofstream stream{path, std::ios::binary | std::ios::trunc};
	// Streamed through an output iterator rather than a cast buffer: no
	// pointer laundering, and a 4 MiB fixture is not a throughput concern.
	std::ranges::transform(image, std::ostreambuf_iterator<char>{stream}, [](std::byte value) {
		return static_cast<char>(value);
	});
	if (!stream.good()) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	return static_cast<std::uint64_t>(image.size());
}

} // namespace revenant::imagegen::ntfs

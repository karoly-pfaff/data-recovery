// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ntfs/FixtureFiles.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/fs/ntfs/Runlist.hpp"

namespace revenant::imagegen::ntfs {

namespace {

using revenant::fs::ntfs::DataRun;

// SOI, a 6-byte APP0, and a 4-byte SOS header open every fixture JPEG; EOI
// closes it. Everything between is entropy-coded payload.
constexpr std::size_t kJpegFrameBytes = 14;
constexpr std::size_t kEntropyModulus = 0xFE; // never produces a raw 0xFF

constexpr std::size_t kKeepJpegBytes = 5000;
constexpr std::size_t kDeletedJpegBytes = 9000;
constexpr std::size_t kOrphanJpegBytes = 3000;
constexpr std::size_t kUnallocatedJpegBytes = 2500;

// Data-area clusters. Spelled out here so the fragmentation of `deleted.jpg`
// is visible as a layout decision rather than buried in a builder.
constexpr std::uint64_t kKeepJpegCluster = 16;
constexpr std::uint64_t kDeletedJpegFirstCluster = 20;
constexpr std::uint64_t kDeletedJpegSecondCluster = 30;
constexpr std::uint64_t kOrphanJpegCluster = 40;

constexpr std::string_view kNotesText =
	"revenant fixture: this file was deleted while its data was still resident.";

void appendEntropy(std::vector<std::byte>& jpeg, std::size_t count) {
	for (std::size_t i = 0; i < count; ++i) {
		jpeg.push_back(static_cast<std::byte>(i % kEntropyModulus));
	}
}

[[nodiscard]] FixtureFile mftFile(const NtfsLayout& layout) {
	return FixtureFile{
		.recordNumber = kMftRecord,
		.name = "$MFT",
		.parentRecord = kRootRecord,
		.inUse = true,
		.isDirectory = false,
		.dataKind = DataKind::kNonResident,
		.runs = {DataRun{
			.startCluster = layout.mftStartCluster,
			.lengthClusters = layout.mftClusterCount(),
			.sparse = false}},
		.content = {}};
}

[[nodiscard]] FixtureFile
directoryFile(std::uint64_t record, std::string_view name, std::uint64_t parent) {
	return FixtureFile{
		.recordNumber = record,
		.name = name,
		.parentRecord = parent,
		.inUse = true,
		.isDirectory = true,
		.dataKind = DataKind::kNone,
		.runs = {},
		.content = {}};
}

[[nodiscard]] FixtureFile keepJpeg() {
	return FixtureFile{
		.recordNumber = kKeepJpegRecord,
		.name = "keep.jpg",
		.parentRecord = kPhotosRecord,
		.inUse = true,
		.isDirectory = false,
		.dataKind = DataKind::kNonResident,
		.runs = {DataRun{.startCluster = kKeepJpegCluster, .lengthClusters = 2, .sparse = false}},
		.content = fixtureJpeg(kKeepJpegBytes)};
}

// The interesting one: deleted, and its data lives in two separate places, so
// recovering it proves the runlist decoder rather than a contiguous read.
[[nodiscard]] FixtureFile deletedJpeg() {
	return FixtureFile{
		.recordNumber = kDeletedJpegRecord,
		.name = "deleted.jpg",
		.parentRecord = kPhotosRecord,
		.inUse = false,
		.isDirectory = false,
		.dataKind = DataKind::kNonResident,
		.runs =
			{DataRun{
				 .startCluster = kDeletedJpegFirstCluster,
				 .lengthClusters = 2,
				 .sparse = false},
			 DataRun{
				 .startCluster = kDeletedJpegSecondCluster,
				 .lengthClusters = 1,
				 .sparse = false}},
		.content = fixtureJpeg(kDeletedJpegBytes)};
}

[[nodiscard]] FixtureFile deletedNotes() {
	const auto text = std::as_bytes(std::span{kNotesText.data(), kNotesText.size()});
	return FixtureFile{
		.recordNumber = kDeletedNotesRecord,
		.name = "notes.txt",
		.parentRecord = kRootRecord,
		.inUse = false,
		.isDirectory = false,
		.dataKind = DataKind::kResident,
		.runs = {},
		.content = std::vector<std::byte>{text.begin(), text.end()}};
}

[[nodiscard]] FixtureFile orphanJpeg() {
	return FixtureFile{
		.recordNumber = kOrphanJpegRecord,
		.name = "orphan.jpg",
		.parentRecord = kMissingParentRecord,
		.inUse = false,
		.isDirectory = false,
		.dataKind = DataKind::kNonResident,
		.runs = {DataRun{.startCluster = kOrphanJpegCluster, .lengthClusters = 1, .sparse = false}},
		.content = fixtureJpeg(kOrphanJpegBytes)};
}

} // namespace

std::vector<std::byte> fixtureJpeg(std::size_t sizeBytes) {
	std::vector<std::byte> jpeg{
		std::byte{0xFF},
		std::byte{0xD8}, // SOI
		std::byte{0xFF},
		std::byte{0xE0},
		std::byte{0x00},
		std::byte{0x04}, // APP0, length 4
		std::byte{0x4A},
		std::byte{0x46}, // "JF"
		std::byte{0xFF},
		std::byte{0xDA},
		std::byte{0x00},
		std::byte{0x02}}; // SOS, length 2
	appendEntropy(jpeg, sizeBytes - kJpegFrameBytes);
	jpeg.push_back(std::byte{0xFF});
	jpeg.push_back(std::byte{0xD9}); // EOI
	return jpeg;
}

std::vector<std::byte> unallocatedJpeg() {
	return fixtureJpeg(kUnallocatedJpegBytes);
}

std::vector<FixtureFile> fixtureFiles(const NtfsLayout& layout) {
	std::vector<FixtureFile> files;
	files.push_back(mftFile(layout));
	files.push_back(directoryFile(kRootRecord, ".", kRootRecord));
	files.push_back(directoryFile(kPhotosRecord, "photos", kRootRecord));
	files.push_back(keepJpeg());
	files.push_back(deletedJpeg());
	files.push_back(deletedNotes());
	files.push_back(orphanJpeg());
	return files;
}

} // namespace revenant::imagegen::ntfs

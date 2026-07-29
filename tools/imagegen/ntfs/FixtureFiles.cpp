// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ntfs/FixtureFiles.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "imagegen/FixtureBytes.hpp"
#include "imagegen/FixtureJpeg.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/fs/ntfs/Runlist.hpp"

namespace revenant::imagegen::ntfs {

namespace {

using revenant::fs::ntfs::DataRun;

// Small on purpose: a filler file exists to be a record the walk has to read,
// and content it does not have is content the volume does not have to hold.
constexpr std::size_t kFillerContentBytes = 48;

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

// A live file with its whole content inside its own record: what a scaled
// volume is padded with, because a resident file needs no data clusters and
// the data region is the one part of the fixture that must not move.
[[nodiscard]] FixtureFile fillerFile(std::uint64_t record) {
	const auto ordinal = std::to_string(record);
	return FixtureFile{
		.recordNumber = record,
		.name = "filler-" + ordinal + ".txt",
		.parentRecord = kRootRecord,
		.inUse = true,
		.isDirectory = false,
		.dataKind = DataKind::kResident,
		.runs = {},
		.content = fixtureContent(kFillerContentBytes, static_cast<std::byte>(record))};
}

// Every record past the fixed fixture's own, which is none of them until a
// benchmark asks for a bigger `$MFT`.
void appendFillers(std::vector<FixtureFile>& files, const NtfsLayout& layout) {
	for (std::uint64_t record = kMftRecordCount; record < layout.mftRecordCount; ++record) {
		files.push_back(fillerFile(record));
	}
}

[[nodiscard]] FixtureFile
directoryFile(std::uint64_t record, std::string_view name, std::uint64_t parent) {
	return FixtureFile{
		.recordNumber = record,
		.name = std::string{name},
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
	appendFillers(files, layout);
	return files;
}

} // namespace revenant::imagegen::ntfs

// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/fat/Fat32Fixture.hpp"

#include <cstddef>
#include <vector>

#include "imagegen/fat/Fat32Layout.hpp"

namespace revenant::imagegen::fat {

namespace {

constexpr std::size_t kNotesBytes = 900;
constexpr std::size_t kDeletedNotesBytes = 3000;
constexpr std::size_t kKeepJpegBytes = 5000;
constexpr std::size_t kDeletedJpegBytes = 3000;
constexpr std::size_t kOrphanJpegBytes = 2500;

[[nodiscard]] Fat32File liveNotes() {
	return Fat32File{
		.shortName = "NOTES   TXT",
		.longName = {},
		.clusters = {kNotesCluster},
		.deleted = false,
		.content = fixtureContent(kNotesBytes, std::byte{1})};
}

// Two clusters' worth, contiguous — which is what makes the undelete guess
// right for this one and worth contrasting with a fragmented file.
[[nodiscard]] Fat32File deletedNotes() {
	return Fat32File{
		.shortName = "DELETED TXT",
		.longName = {},
		.clusters = {kDeletedNotesCluster, kDeletedNotesCluster + 1},
		.deleted = true,
		.content = fixtureContent(kDeletedNotesBytes, std::byte{2})};
}

// The interesting live one: a long name, and content in two separate places, so
// recovering it proves the chain is followed rather than assumed contiguous.
[[nodiscard]] Fat32File keepPhoto() {
	return Fat32File{
		.shortName = "KEEP~1  JPG",
		.longName = "keep-photo.jpg",
		.clusters = {kKeepJpegCluster, kKeepJpegSecondCluster, kKeepJpegSecondCluster + 1},
		.deleted = false,
		.content = fixtureContent(kKeepJpegBytes, std::byte{3})};
}

[[nodiscard]] Fat32File deletedPhoto() {
	return Fat32File{
		.shortName = "DELETED JPG",
		.longName = {},
		.clusters = {kDeletedJpegCluster, kDeletedJpegCluster + 1},
		.deleted = true,
		.content = fixtureContent(kDeletedJpegBytes, std::byte{4})};
}

[[nodiscard]] Fat32File orphanPhoto() {
	return Fat32File{
		.shortName = "ORPHAN  JPG",
		.longName = {},
		.clusters = {kOrphanJpegCluster, kOrphanJpegCluster + 1},
		.deleted = true,
		.content = fixtureContent(kOrphanJpegBytes, std::byte{5})};
}

} // namespace

std::vector<std::byte> fixtureContent(std::size_t sizeBytes, std::byte seed) {
	const auto offset = std::to_integer<std::size_t>(seed);
	std::vector<std::byte> content;
	content.reserve(sizeBytes);
	for (std::size_t at = 0; at < sizeBytes; ++at) {
		content.push_back(static_cast<std::byte>((at + offset) % 251U));
	}
	return content;
}

std::vector<Fat32File> fat32FixtureFiles() {
	std::vector<Fat32File> files;
	files.push_back(liveNotes());
	files.push_back(deletedNotes());
	files.push_back(keepPhoto());
	files.push_back(deletedPhoto());
	files.push_back(orphanPhoto());
	return files;
}

} // namespace revenant::imagegen::fat

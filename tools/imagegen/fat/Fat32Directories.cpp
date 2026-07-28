// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/fat/Fat32Directories.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "imagegen/ByteWriter.hpp"
#include "imagegen/fat/Fat32Entries.hpp"
#include "imagegen/fat/Fat32Layout.hpp"

namespace revenant::imagegen::fat {

namespace {

// A long name is stored across as many 13-code-unit fragments as it needs.
constexpr std::size_t kUnitsPerFragment = 13;

constexpr std::string_view kKeepLongName = "keep-photo.jpg";
constexpr std::string_view kKeepShortName = "KEEP~1  JPG";

constexpr std::uint32_t kNotesBytes = 900;
constexpr std::uint32_t kDeletedNotesBytes = 3000;
constexpr std::uint32_t kKeepJpegBytes = 5000;
constexpr std::uint32_t kDeletedJpegBytes = 3000;
constexpr std::uint32_t kOrphanJpegBytes = 2500;

// A directory under construction: slots appended in the order a walk will meet
// them.
using Slots = std::vector<std::byte>;

void append(Slots& slots, const std::vector<std::byte>& slot) {
	slots.insert(slots.end(), slot.begin(), slot.end());
}

void appendFile(Slots& slots, const SlotSpec& spec) {
	append(slots, shortSlot(spec));
}

// The dot entries every directory but the root opens with.
void appendDots(Slots& slots, std::uint32_t self) {
	appendFile(
		slots,
		SlotSpec{
			.shortName = ".          ",
			.attributes = kAttrDirectory,
			.caseFlags = 0,
			.firstCluster = self,
			.sizeInBytes = 0,
			.deleted = false});
	appendFile(
		slots,
		SlotSpec{
			.shortName = "..         ",
			.attributes = kAttrDirectory,
			.caseFlags = 0,
			.firstCluster = 0,
			.sizeInBytes = 0,
			.deleted = false});
}

// Fragments are stored last-first, immediately before the short entry, and the
// physically first one carries the last-fragment flag.
void appendLongName(Slots& slots, std::string_view name, std::uint8_t checksum) {
	const auto fragments = ((name.size() - 1) / kUnitsPerFragment) + 1;
	for (std::size_t ordinal = fragments; ordinal >= 1; --ordinal) {
		const auto from = (ordinal - 1) * kUnitsPerFragment;
		const auto flag = ordinal == fragments ? 0x40U : 0x00U;
		append(
			slots,
			longNameSlot(
				static_cast<std::uint8_t>(ordinal | flag),
				name.substr(from, kUnitsPerFragment),
				checksum));
	}
}

[[nodiscard]] Slots rootSlots() {
	Slots slots;
	appendFile(
		slots,
		SlotSpec{
			.shortName = "PHOTOS     ",
			.attributes = kAttrDirectory,
			.caseFlags = kLowerCaseBase,
			.firstCluster = kPhotosCluster,
			.sizeInBytes = 0,
			.deleted = false});
	appendFile(
		slots,
		SlotSpec{
			.shortName = "NOTES   TXT",
			.attributes = kAttrArchive,
			.caseFlags = 0,
			.firstCluster = kNotesCluster,
			.sizeInBytes = kNotesBytes,
			.deleted = false});
	appendFile(
		slots,
		SlotSpec{
			.shortName = "DELETED TXT",
			.attributes = kAttrArchive,
			.caseFlags = 0,
			.firstCluster = kDeletedNotesCluster,
			.sizeInBytes = kDeletedNotesBytes,
			.deleted = true});
	appendFile(
		slots,
		SlotSpec{
			.shortName = "GONE       ",
			.attributes = kAttrDirectory,
			.caseFlags = 0,
			.firstCluster = kGoneDirCluster,
			.sizeInBytes = 0,
			.deleted = true});
	return slots;
}

[[nodiscard]] Slots photosSlots() {
	Slots slots;
	appendDots(slots, kPhotosCluster);
	appendLongName(slots, kKeepLongName, shortNameChecksum(kKeepShortName));
	appendFile(
		slots,
		SlotSpec{
			.shortName = kKeepShortName,
			.attributes = kAttrArchive,
			.caseFlags = 0,
			.firstCluster = kKeepJpegCluster,
			.sizeInBytes = kKeepJpegBytes,
			.deleted = false});
	appendFile(
		slots,
		SlotSpec{
			.shortName = "DELETED JPG",
			.attributes = kAttrArchive,
			.caseFlags = 0,
			.firstCluster = kDeletedJpegCluster,
			.sizeInBytes = kDeletedJpegBytes,
			.deleted = true});
	return slots;
}

[[nodiscard]] Slots goneSlots() {
	Slots slots;
	appendDots(slots, kGoneDirCluster);
	appendFile(
		slots,
		SlotSpec{
			.shortName = "ORPHAN  JPG",
			.attributes = kAttrArchive,
			.caseFlags = 0,
			.firstCluster = kOrphanJpegCluster,
			.sizeInBytes = kOrphanJpegBytes,
			.deleted = true});
	return slots;
}

void putAt(
	std::vector<std::byte>& image,
	const Fat32Layout& layout,
	std::uint32_t cluster,
	const Slots& slots) {
	putBytes(image, static_cast<std::size_t>(layout.clusterOffsetBytes(cluster)), slots);
}

} // namespace

void putDirectories(std::vector<std::byte>& image, const Fat32Layout& layout) {
	putAt(image, layout, kRootCluster, rootSlots());
	putAt(image, layout, kPhotosCluster, photosSlots());
	putAt(image, layout, kGoneDirCluster, goneSlots());
}

} // namespace revenant::imagegen::fat

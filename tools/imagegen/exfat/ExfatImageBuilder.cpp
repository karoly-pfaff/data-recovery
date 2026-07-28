// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/exfat/ExfatImageBuilder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "imagegen/ByteWriter.hpp"
#include "imagegen/ClusterContent.hpp"
#include "imagegen/exfat/ExfatEntries.hpp"
#include "imagegen/exfat/ExfatFixture.hpp"
#include "imagegen/exfat/ExfatLayout.hpp"

namespace revenant::imagegen::exfat {

namespace {

constexpr std::uint32_t kEndOfChain = 0x0FFF'FFFF;
constexpr std::uint32_t kFirstDataCluster = 2;

// The clusters the volume considers in use. `kOverwrittenCluster` is in the
// list although the file that had it was deleted — that is the whole point of
// it: the volume handed it out again, so what is there now is not what the
// deleted entry claims.
constexpr std::array<std::uint32_t, 8> kAllocated{
	kRootCluster,
	kBitmapCluster,
	kKeepCluster,
	kKeepSecondCluster,
	kPhotosCluster,
	kInnerCluster,
	kOverwrittenCluster,
	kFirstDataCluster};

void putBootSector(std::vector<std::byte>& image, const ExfatLayout& layout) {
	putBytes(image, 0x03, std::as_bytes(std::span{std::string_view{"EXFAT   "}}));
	putLe<std::uint64_t>(image, 0x48, layout.volumeSectors);
	putLe<std::uint32_t>(image, 0x50, layout.fatSector);
	putLe<std::uint32_t>(image, 0x54, layout.fatSectors);
	putLe<std::uint32_t>(image, 0x58, layout.heapSector);
	putLe<std::uint32_t>(image, 0x5C, layout.clusterCount);
	putLe<std::uint32_t>(image, 0x60, layout.rootCluster);
	putLe<std::uint16_t>(image, 0x1FE, 0xAA55);
}

void putShifts(std::vector<std::byte>& image) {
	putLe<std::uint8_t>(image, 0x6C, 9);
	putLe<std::uint8_t>(image, 0x6D, 3);
	putLe<std::uint8_t>(image, 0x6E, 1);
}

// One table entry as the builder states it: which cluster, and where it points.
struct FatEntry {
	std::uint32_t cluster;
	std::uint32_t next;
};

void putFatEntry(std::vector<std::byte>& image, const ExfatLayout& layout, FatEntry entry) {
	const auto at = layout.fatOffsetBytes() + (std::uint64_t{entry.cluster} * 4);
	putLe<std::uint32_t>(image, static_cast<std::size_t>(at), entry.next);
}

// A file that declared itself contiguous keeps nothing in the table; one that
// did not is chained through it, which is what the live fragmented file proves.
void putChain(
	std::vector<std::byte>& image,
	const ExfatLayout& layout,
	const std::vector<std::uint32_t>& clusters) {
	putClusterChain(clusters, kEndOfChain, [&](ChainLink link) {
		putFatEntry(image, layout, FatEntry{.cluster = link.cluster, .next = link.next});
	});
}

void putBitmap(std::vector<std::byte>& image, const ExfatLayout& layout) {
	const auto at = static_cast<std::size_t>(layout.clusterOffsetBytes(kBitmapCluster));
	for (const std::uint32_t cluster : kAllocated) {
		const std::size_t index = cluster - kFirstDataCluster;
		const auto byte = std::to_integer<unsigned>(image.at(at + (index / 8)));
		image.at(at + (index / 8)) = static_cast<std::byte>(byte | (1U << (index % 8)));
	}
}

void putContent(std::vector<std::byte>& image, const ExfatLayout& layout, const ExfatFile& file) {
	putClusteredContent(
		image,
		file.content,
		file.clusters,
		layout.bytesPerCluster(),
		[&layout](std::uint32_t cluster) { return layout.clusterOffsetBytes(cluster); });
}

[[nodiscard]] SetSpec specOf(const ExfatFile& file) {
	return SetSpec{
		.name = file.name,
		.firstCluster = file.clusters.front(),
		.sizeInBytes = file.content.size(),
		.live = file.live,
		.contiguous = file.contiguous,
		.isDirectory = false};
}

void appendSet(std::vector<std::byte>& slots, const SetSpec& spec) {
	const auto set = entrySet(spec);
	slots.insert(slots.end(), set.begin(), set.end());
}

void putFiles(
	std::vector<std::byte>& image,
	const ExfatLayout& layout,
	const std::vector<ExfatFile>& files) {
	for (const ExfatFile& file : files) {
		putContent(image, layout, file);
		if (!file.contiguous) {
			putChain(image, layout, file.clusters);
		}
	}
}

[[nodiscard]] std::vector<std::byte> rootSlots(const ExfatLayout& layout) {
	std::vector<std::byte> slots = bitmapEntry(
		BitmapSpec{.firstCluster = kBitmapCluster, .lengthBytes = (layout.clusterCount + 7) / 8});
	for (const ExfatFile& file : rootFiles()) {
		appendSet(slots, specOf(file));
	}
	appendSet(
		slots,
		SetSpec{
			.name = "photos",
			.firstCluster = kPhotosCluster,
			.sizeInBytes = 0,
			.live = true,
			.contiguous = true,
			.isDirectory = true});
	return slots;
}

[[nodiscard]] std::vector<std::byte> photosSlots() {
	std::vector<std::byte> slots;
	for (const ExfatFile& file : photosFiles()) {
		appendSet(slots, specOf(file));
	}
	return slots;
}

void putDirectories(std::vector<std::byte>& image, const ExfatLayout& layout) {
	putBytes(
		image,
		static_cast<std::size_t>(layout.clusterOffsetBytes(kRootCluster)),
		rootSlots(layout));
	putBytes(
		image,
		static_cast<std::size_t>(layout.clusterOffsetBytes(kPhotosCluster)),
		photosSlots());
}

// Every directory and the bitmap are single-cluster chains; the files that
// declared themselves contiguous keep nothing in the table at all.
void putDirectoryChains(std::vector<std::byte>& image, const ExfatLayout& layout) {
	putFatEntry(image, layout, FatEntry{.cluster = kRootCluster, .next = kEndOfChain});
	putFatEntry(image, layout, FatEntry{.cluster = kBitmapCluster, .next = kEndOfChain});
	putFatEntry(image, layout, FatEntry{.cluster = kPhotosCluster, .next = kEndOfChain});
}

} // namespace

std::vector<std::byte> buildExfatImage() {
	const auto layout = makeExfatLayout();
	std::vector<std::byte> image(static_cast<std::size_t>(layout.totalBytes()), std::byte{0});
	putBootSector(image, layout);
	putShifts(image);
	putDirectoryChains(image, layout);
	putDirectories(image, layout);
	putFiles(image, layout, rootFiles());
	putFiles(image, layout, photosFiles());
	putBitmap(image, layout);
	return image;
}

} // namespace revenant::imagegen::exfat

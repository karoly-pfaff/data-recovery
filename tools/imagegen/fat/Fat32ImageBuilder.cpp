// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/fat/Fat32ImageBuilder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "imagegen/ByteWriter.hpp"
#include "imagegen/fat/Fat32Directories.hpp"
#include "imagegen/fat/Fat32Fixture.hpp"
#include "imagegen/fat/Fat32Layout.hpp"

namespace revenant::imagegen::fat {

namespace {

constexpr std::uint32_t kEndOfChain = 0x0FFF'FFFF;
constexpr std::uint32_t kMediaDescriptorEntry = 0x0FFF'FFF8;

void putBootSector(std::vector<std::byte>& image, const Fat32Layout& layout) {
	putLe<std::uint16_t>(image, 0x0B, static_cast<std::uint16_t>(layout.bytesPerSector));
	putLe<std::uint8_t>(image, 0x0D, static_cast<std::uint8_t>(layout.sectorsPerCluster));
	putLe<std::uint16_t>(image, 0x0E, static_cast<std::uint16_t>(layout.reservedSectors));
	putLe<std::uint8_t>(image, 0x10, static_cast<std::uint8_t>(layout.fatCount));
	putLe<std::uint32_t>(image, 0x20, layout.totalSectors);
	putLe<std::uint32_t>(image, 0x24, layout.fatSectors);
	putLe<std::uint32_t>(image, 0x2C, layout.rootCluster);
	putBytes(image, 0x52, std::as_bytes(std::span{std::string_view{"FAT32   "}}));
	putLe<std::uint16_t>(image, 0x1FE, 0xAA55);
}

// One FAT entry as the builder states it: which cluster, and where it points.
struct FatEntry {
	std::uint32_t cluster;
	std::uint32_t next;
};

// Written into every copy of the FAT — a real volume keeps them identical, and
// a parser that read the second one must see the same answer.
void putFatEntry(std::vector<std::byte>& image, const Fat32Layout& layout, FatEntry entry) {
	for (std::uint32_t fat = 0; fat < layout.fatCount; ++fat) {
		const auto at = layout.fatOffsetBytes(fat) + (std::uint64_t{entry.cluster} * 4);
		putLe<std::uint32_t>(image, static_cast<std::size_t>(at), entry.next);
	}
}

// A live file's clusters are chained; a deleted file's are left free, which is
// exactly what deletion does and what makes its extents a guess.
void putChain(
	std::vector<std::byte>& image,
	const Fat32Layout& layout,
	const std::vector<std::uint32_t>& clusters) {
	for (std::size_t at = 0; at + 1 < clusters.size(); ++at) {
		putFatEntry(
			image,
			layout,
			FatEntry{.cluster = clusters.at(at), .next = clusters.at(at + 1)});
	}
	putFatEntry(image, layout, FatEntry{.cluster = clusters.back(), .next = kEndOfChain});
}

// One cluster's share of a file's content, or nothing if the file ran out
// before this cluster.
[[nodiscard]] std::span<const std::byte>
shareOf(const Fat32File& file, std::size_t from, std::size_t clusterBytes) {
	if (from >= file.content.size()) {
		return {};
	}
	const auto count = std::min<std::size_t>(clusterBytes, file.content.size() - from);
	return std::span{file.content}.subspan(from, count);
}

void putContent(std::vector<std::byte>& image, const Fat32Layout& layout, const Fat32File& file) {
	const auto clusterBytes = layout.bytesPerCluster();
	for (std::size_t at = 0; at < file.clusters.size(); ++at) {
		const auto share = shareOf(file, at * clusterBytes, clusterBytes);
		const auto offset = layout.clusterOffsetBytes(file.clusters.at(at));
		putBytes(image, static_cast<std::size_t>(offset), share);
	}
}

void putFiles(std::vector<std::byte>& image, const Fat32Layout& layout) {
	for (const Fat32File& file : fat32FixtureFiles()) {
		putContent(image, layout, file);
		if (!file.deleted) {
			putChain(image, layout, file.clusters);
		}
	}
}

// The FAT's first two entries are reserved: the media descriptor and the
// volume's dirty flags. No file ever lives in them.
void putReservedEntries(std::vector<std::byte>& image, const Fat32Layout& layout) {
	putFatEntry(image, layout, FatEntry{.cluster = 0, .next = kMediaDescriptorEntry});
	putFatEntry(image, layout, FatEntry{.cluster = 1, .next = kEndOfChain});
}

// A directory is a chain like any other file's. The live ones are one cluster
// each; the deleted `gone` is left free, which is what deletion does to it and
// what leaves its first cluster as the only place its entries can be read from.
void putDirectoryChains(std::vector<std::byte>& image, const Fat32Layout& layout) {
	putFatEntry(image, layout, FatEntry{.cluster = kRootCluster, .next = kEndOfChain});
	putFatEntry(image, layout, FatEntry{.cluster = kPhotosCluster, .next = kEndOfChain});
}

} // namespace

std::vector<std::byte> buildFat32Image() {
	const auto layout = makeFat32Layout();
	std::vector<std::byte> image(static_cast<std::size_t>(layout.totalBytes()), std::byte{0});
	putBootSector(image, layout);
	putReservedEntries(image, layout);
	putDirectoryChains(image, layout);
	putDirectories(image, layout);
	putFiles(image, layout);
	return image;
}

} // namespace revenant::imagegen::fat

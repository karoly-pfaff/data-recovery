// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ext4/Ext4ImageBuilder.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "imagegen/ByteWriter.hpp"
#include "imagegen/ClusterContent.hpp"
#include "imagegen/ext4/Ext4Directories.hpp"
#include "imagegen/ext4/Ext4Fixture.hpp"
#include "imagegen/ext4/Ext4Journal.hpp"
#include "imagegen/ext4/Ext4Layout.hpp"
#include "imagegen/ext4/Ext4Metadata.hpp"
#include "imagegen/ext4/Ext4Records.hpp"

namespace revenant::imagegen::ext4 {

namespace {

constexpr std::uint16_t kRegularFileMode = 0x81A4; // -rw-r--r--
constexpr std::uint16_t kDirectoryMode = 0x41ED;   // drwxr-xr-x
constexpr std::uint32_t kDeletionSeconds = 1'596'290'000;

// A deleted file keeps its mode, its size and its times, and loses its link
// count — and, on the kernels this fixture models, its extent tree.
[[nodiscard]] InodeSpec fileInode(const Ext4File& file) {
	return InodeSpec{
		.mode = kRegularFileMode,
		.links = file.live ? std::uint16_t{1} : std::uint16_t{0},
		.sizeInBytes = file.content.size(),
		.deletionTime = file.live ? 0 : kDeletionSeconds,
		.runs = file.treeWiped ? std::vector<ExtentSpec>{} : file.runs};
}

[[nodiscard]] InodeSpec directoryInode(std::uint32_t block, std::uint32_t blockSizeBytes) {
	return InodeSpec{
		.mode = kDirectoryMode,
		.links = 2,
		.sizeInBytes = blockSizeBytes,
		.deletionTime = 0,
		.runs = {ExtentSpec{.firstFileBlock = 0, .blockCount = 1, .firstDeviceBlock = block}}};
}

void putFile(std::vector<std::byte>& image, const Ext4Layout& layout, const Ext4File& file) {
	putExt4Inode(image, layout, file.inode, fileInode(file));
	putClusteredContent(
		image,
		file.content,
		blocksOf(file.runs),
		layout.blockSizeBytes,
		[&layout](std::uint32_t block) { return layout.blockOffset(block); });
}

void putFiles(
	std::vector<std::byte>& image,
	const Ext4Layout& layout,
	const std::vector<Ext4File>& files) {
	for (const Ext4File& file : files) {
		putFile(image, layout, file);
	}
}

void putDirectories(std::vector<std::byte>& image, const Ext4Layout& layout) {
	putExt4Inode(image, layout, kRootInode, directoryInode(kRootDirBlock, layout.blockSizeBytes));
	putExt4Inode(
		image,
		layout,
		kPhotosInode,
		directoryInode(kPhotosDirBlock, layout.blockSizeBytes));
	putBytes(
		image,
		static_cast<std::size_t>(layout.blockOffset(kRootDirBlock)),
		rootDirectoryBlock(layout));
	putBytes(
		image,
		static_cast<std::size_t>(layout.blockOffset(kPhotosDirBlock)),
		photosDirectoryBlock(layout));
}

// The inode table block that holds the wiped file's inode, as it stood before
// the deletion: its extent tree still there. This is what the journal kept.
[[nodiscard]] RememberedBlock
rememberedInodeBlock(const Ext4Layout& layout, const Ext4File& wiped) {
	const auto at = layout.inodeOffset(wiped.inode);
	std::vector<std::byte> block(layout.blockSizeBytes, std::byte{0});
	putBytes(
		block,
		static_cast<std::size_t>(at % layout.blockSizeBytes),
		inodeRecord(
			InodeSpec{
				.mode = kRegularFileMode,
				.links = 1,
				.sizeInBytes = wiped.content.size(),
				.deletionTime = 0,
				.runs = wiped.runs},
			layout.inodeSizeBytes));
	return RememberedBlock{
		.fileSystemBlock = static_cast<std::uint32_t>(at / layout.blockSizeBytes),
		.content = std::move(block)};
}

[[nodiscard]] std::vector<ExtentSpec> journalRuns() {
	return {ExtentSpec{
		.firstFileBlock = 0,
		.blockCount = kJournalBlocks,
		.firstDeviceBlock = kJournalFirstBlock}};
}

void putJournal(std::vector<std::byte>& image, const Ext4Layout& layout, const Ext4File& wiped) {
	putExt4Inode(
		image,
		layout,
		kJournalInode,
		InodeSpec{
			.mode = kRegularFileMode,
			.links = 1,
			.sizeInBytes = std::uint64_t{kJournalBlocks} * layout.blockSizeBytes,
			.deletionTime = 0,
			.runs = journalRuns()});
	const auto blocks = journalBlocks(rememberedInodeBlock(layout, wiped), layout.blockSizeBytes);
	for (std::size_t at = 0; at < blocks.size(); ++at) {
		putBytes(
			image,
			static_cast<std::size_t>(layout.blockOffset(kJournalFirstBlock + at)),
			blocks.at(at));
	}
}

[[nodiscard]] const Ext4File& wipedFile(const std::vector<Ext4File>& files) {
	for (const Ext4File& file : files) {
		if (file.treeWiped) {
			return file;
		}
	}
	return files.front();
}

} // namespace

std::vector<std::byte> buildExt4Image() {
	const auto layout = makeExt4Layout();
	auto image = emptyExt4Volume(layout);
	const auto root = rootFiles();
	putDirectories(image, layout);
	putFiles(image, layout, root);
	putFiles(image, layout, photosFiles());
	putFile(image, layout, orphanFile());
	putJournal(image, layout, wipedFile(root));
	return image;
}

} // namespace revenant::imagegen::ext4

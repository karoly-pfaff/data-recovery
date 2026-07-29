// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ext4/Ext4Directories.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "imagegen/ByteWriter.hpp"
#include "imagegen/ext4/Ext4Fixture.hpp"
#include "imagegen/ext4/Ext4Layout.hpp"
#include "imagegen/ext4/Ext4Records.hpp"

namespace revenant::imagegen::ext4 {

namespace {

constexpr std::uint8_t kRegularFileType = 1;
constexpr std::uint8_t kDirectoryType = 2;

// A live entry and the deleted one its record swallowed. `recordBytes` is the
// grown length: the live entry's own bytes plus the whole of the hidden one.
struct SwallowedPair {
	DirEntrySpec live{};
	DirEntrySpec hidden{};
	std::size_t at{};
};

void putEntry(std::vector<std::byte>& block, std::size_t at, const DirEntrySpec& spec) {
	putBytes(block, at, dirEntry(spec));
}

// The hidden entry goes at the end of the live one's own bytes, which is where
// it was before its neighbour's record grew over it.
void putPair(std::vector<std::byte>& block, const SwallowedPair& pair) {
	putEntry(block, pair.at, pair.live);
	putEntry(block, pair.at + pair.live.recordBytes - pair.hidden.recordBytes, pair.hidden);
}

[[nodiscard]] DirEntrySpec
dot(std::string_view name, std::uint32_t inode, std::uint16_t recordBytes) {
	return DirEntrySpec{
		.inode = inode,
		.name = name,
		.fileType = kDirectoryType,
		.recordBytes = recordBytes};
}

// `keep.txt` grew over the deleted `gone.txt`.
[[nodiscard]] SwallowedPair keepPair() {
	return SwallowedPair{
		.live =
			DirEntrySpec{
				.inode = kKeepInode,
				.name = "keep.txt",
				.fileType = kRegularFileType,
				.recordBytes = 32},
		.hidden =
			DirEntrySpec{
				.inode = kGoneInode,
				.name = "gone.txt",
				.fileType = kRegularFileType,
				.recordBytes = 16},
		.at = 24};
}

// `photos` grew over the deleted `wiped.txt`, whose inode kept its name and lost
// its extent tree.
[[nodiscard]] SwallowedPair photosPair() {
	return SwallowedPair{
		.live =
			DirEntrySpec{
				.inode = kPhotosInode,
				.name = "photos",
				.fileType = kDirectoryType,
				.recordBytes = 36},
		.hidden =
			DirEntrySpec{
				.inode = kWipedInode,
				.name = "wiped.txt",
				.fileType = kRegularFileType,
				.recordBytes = 20},
		.at = 56};
}

// `later.bin` grew over a deleted name whose inode the volume has since handed
// back out — to `later.bin` itself, which is why the name is real and the bytes
// behind it are not.
[[nodiscard]] SwallowedPair laterPair(std::uint16_t recordBytes) {
	return SwallowedPair{
		.live =
			DirEntrySpec{
				.inode = kLaterInode,
				.name = "later.bin",
				.fileType = kRegularFileType,
				.recordBytes = recordBytes},
		.hidden =
			DirEntrySpec{
				.inode = kLaterInode,
				.name = kReusedName,
				.fileType = kRegularFileType,
				.recordBytes = 20},
		.at = 92};
}

} // namespace

std::vector<std::byte> rootDirectoryBlock(const Ext4Layout& layout) {
	std::vector<std::byte> block(layout.blockSizeBytes, std::byte{0});
	putEntry(block, 0, dot(".", kRootInode, 12));
	putEntry(block, 12, dot("..", kRootInode, 12));
	putPair(block, keepPair());
	putPair(block, photosPair());
	putPair(block, laterPair(static_cast<std::uint16_t>(layout.blockSizeBytes - 92)));
	return block;
}

std::vector<std::byte> photosDirectoryBlock(const Ext4Layout& layout) {
	std::vector<std::byte> block(layout.blockSizeBytes, std::byte{0});
	putEntry(block, 0, dot(".", kPhotosInode, 12));
	putEntry(block, 12, dot("..", kRootInode, 12));
	putEntry(
		block,
		24,
		DirEntrySpec{
			.inode = kInnerInode,
			.name = "inner.bin",
			.fileType = kRegularFileType,
			.recordBytes = static_cast<std::uint16_t>(layout.blockSizeBytes - 24)});
	return block;
}

} // namespace revenant::imagegen::ext4

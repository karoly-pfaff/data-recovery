// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: any bytes offered as a jbd2 block parse without a crash or a read
// past the block. The journal's own feature flags decide how wide a descriptor
// tag is, so the tags are read at every width the superblock can ask for —
// including on bytes that were never a descriptor. That is exactly the case a
// crafted journal would produce, and the one where a tag walked at the wrong
// stride would run off the end.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "fs/ext4/JournalFormat.hpp"

namespace {

using revenant::fs::ext4::JournalHead;

// The three tag shapes jbd2 has, whatever this block's own superblock says.
constexpr std::array<std::size_t, 3> kTagWidths{8, 12, 16};

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const auto block = std::as_bytes(std::span{data, size});
	(void)revenant::fs::ext4::parseJournalSuperblock(block);
	for (const std::size_t tagBytes : kTagWidths) {
		const JournalHead head{
			.blockSizeBytes = static_cast<std::uint32_t>(size),
			.maxBlocks = 0,
			.firstBlock = 1,
			.tagBytes = tagBytes,
			.hasBlockTail = tagBytes != 8};
		(void)revenant::fs::ext4::parseDescriptorTags(block, head);
	}
	return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: any bytes offered as an ext4 inode parse without a crash or a read
// past the slot. The inode's own `i_extra_isize` decides whether a field beyond
// the fixed 128 bytes is there at all, so a slot that lies about it is exactly
// the case worth surviving. The group descriptor beside it is driven on the same
// bytes at both of its widths.
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/fs/ext4/GroupDescriptor.hpp"
#include "revenant/fs/ext4/Inode.hpp"

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const auto slot = std::as_bytes(std::span{data, size});
	(void)revenant::fs::ext4::parseExt4Inode(slot);
	(void)revenant::fs::ext4::parseGroupDescriptor(slot, revenant::fs::ext4::kSmallDescriptorBytes);
	(void)revenant::fs::ext4::parseGroupDescriptor(slot, revenant::fs::ext4::kWideDescriptorBytes);
	return 0;
}

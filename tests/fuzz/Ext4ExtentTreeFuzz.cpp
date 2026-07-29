// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: any bytes offered as an extent-tree node parse without a crash or a
// read past the node. Both entry readers are driven whatever the node's depth
// says, because a walk that mistook a leaf for an interior node — or a node that
// claims more entries than it has room for — is the case worth surviving.
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/fs/ext4/ExtentTree.hpp"

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const auto node = std::as_bytes(std::span{data, size});
	(void)revenant::fs::ext4::parseExtentHeader(node);
	(void)revenant::fs::ext4::parseExtentLeaves(node);
	(void)revenant::fs::ext4::parseExtentIndices(node);
	return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: any bytes offered as an ext4 superblock parse to a geometry or a
// typed error, never a crash. Every number a walk later trusts — block size,
// group count, where the descriptors are — is derived here from attacker-chosen
// fields, so this is where an unchecked shift or a wrapped product would show up
// as undefined behaviour under the sanitizers.
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/fs/ext4/Superblock.hpp"

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const auto bytes = std::as_bytes(std::span{data, size});
	(void)revenant::fs::ext4::parseExt4Superblock(bytes);
	return 0;
}

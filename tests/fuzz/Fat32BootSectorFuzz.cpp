// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: any bytes offered as a FAT32 boot sector parse to a geometry or a
// typed error, never a crash or a read past the span. Every value the geometry
// is derived from comes from the input, which is where a crafted cluster count
// or FAT size would wrap an offset.
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/fs/fat/BootSector.hpp"

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const auto bytes = std::as_bytes(std::span{data, size});
	(void)revenant::fs::fat::parseFat32BootSector(bytes);
	return 0;
}

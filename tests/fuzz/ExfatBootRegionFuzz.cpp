// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: any bytes offered as an exFAT boot sector parse to a geometry or a
// typed error, never a crash. exFAT states its geometry as log2 exponents, so
// this is also where an unchecked shift by an attacker-chosen exponent would
// show up as undefined behaviour under the sanitizers.
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/fs/exfat/BootRegion.hpp"

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const auto bytes = std::as_bytes(std::span{data, size});
	(void)revenant::fs::exfat::parseExfatBootSector(bytes);
	return 0;
}

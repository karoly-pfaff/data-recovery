// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: any 32 bytes offered as an exFAT directory slot classify and parse
// without a crash or a read past the slot. Every parser is driven whatever the
// type byte says, because a walk that mistook one entry for another is exactly
// the case worth surviving.
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/fs/exfat/DirectoryEntry.hpp"

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const auto slot = std::as_bytes(std::span{data, size});
	(void)revenant::fs::exfat::classifyExfatEntry(slot);
	(void)revenant::fs::exfat::parseFileEntry(slot);
	(void)revenant::fs::exfat::parseStreamExtension(slot);
	(void)revenant::fs::exfat::parseFileName(slot);
	return 0;
}

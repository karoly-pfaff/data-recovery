// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: any 32 bytes offered as a FAT directory slot classify and parse
// without a crash, and every name that comes back is valid UTF-8 (ADR-0010) —
// the slot's name bytes are attacker-controlled, and a decoder that emitted a
// malformed sequence would push the damage downstream into a path. No project
// assertion macro exists to depend on, so a bare `std::abort()` is used
// deliberately: libFuzzer sees an unambiguous crash and keeps the input.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

#include "revenant/fs/fat/DirectoryEntry.hpp"
#include "support/Utf8Check.hpp"

namespace {

void checkShortEntry(std::span<const std::byte> slot) {
	const auto entry = revenant::fs::fat::parseShortEntry(slot);
	if (entry.hasValue() && !revenant::testing::isValidUtf8(entry.value().name.utf8)) {
		std::abort();
	}
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const auto slot = std::as_bytes(std::span{data, size});
	(void)revenant::fs::fat::classifyEntry(slot);
	(void)revenant::fs::fat::parseLongNameFragment(slot);
	checkShortEntry(slot);
	return 0;
}

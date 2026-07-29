// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: any bytes offered as an ext4 directory entry parse without a crash,
// and every name that comes back is valid UTF-8 (ADR-0010). ext4 enforces no
// encoding on a name, and a *deleted* entry's bytes are whatever was lying in
// the directory's hole — so the decoder is the only thing between arbitrary
// bytes and a path. No project assertion macro exists to depend on, so a bare
// `std::abort()` is used deliberately: libFuzzer sees an unambiguous crash and
// keeps the input.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

#include "revenant/fs/NameDecode.hpp"
#include "revenant/fs/ext4/DirectoryEntry.hpp"
#include "support/Utf8Check.hpp"

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const auto bytes = std::as_bytes(std::span{data, size});
	const auto entry = revenant::fs::ext4::parseExt4DirEntry(bytes);
	if (!entry.hasValue()) {
		return 0;
	}
	if (!revenant::testing::isValidUtf8(
			revenant::fs::decodeRawName(entry.value().nameBytes).utf8)) {
		std::abort();
	}
	return 0;
}

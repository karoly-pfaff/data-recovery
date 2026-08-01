// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: any bytes offered as a mount table answer with a mount source or
// with nothing — never a crash, and never a walk that does not end.
//
// This text is the kernel's own `/proc/self/mountinfo` rather than an
// attacker's, so the threat model is not the one the format parsers face. It is
// fuzzed for three other reasons: it splits fields, indexes past a separator
// and unescapes octal entirely by hand; a defect in exactly that code was found
// by eye during story-0609's self-audit and was undefined behaviour; and a
// defect here does not surface as a crash in production but as a destination on
// the disk being recovered being allowed. A parser whose failures are silent is
// the one worth handing hostile bytes to.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "core/io/MountTable.hpp"

namespace {

[[nodiscard]] std::string textOf(std::span<const std::uint8_t> bytes) {
	std::string text;
	text.reserve(bytes.size());
	for (const std::uint8_t value : bytes) {
		text.push_back(static_cast<char>(value));
	}
	return text;
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const std::span<const std::uint8_t> input{data, size};
	if (input.empty()) {
		return 0;
	}
	// The first byte says how much of the rest is the path being asked about;
	// what follows is the table. Both halves are worth varying: the covering
	// rule compares them element by element, so an adversarial path reaches as
	// much of the decision as an adversarial table does.
	const std::size_t split = std::size_t{input.front()} % input.size();
	const auto path = textOf(input.subspan(1, split));
	const auto table = textOf(input.subspan(1 + split));
	static_cast<void>(revenant::mountSourceFor(table, path, input.front()));
	static_cast<void>(revenant::holdsNoLocalStorage(path));
	return 0;
}

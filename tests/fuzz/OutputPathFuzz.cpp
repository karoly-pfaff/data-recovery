// SPDX-License-Identifier: GPL-3.0-or-later
// Invariant: arbitrary bytes as `relativeName`, sanitized against a fixed
// root, yield either a typed error or a path lexically contained within
// that root — never anything else (ADR-0009). Containment is re-verified
// here independently of sanitizeOutputPath's own internal check, using the
// same plain string-prefix convention the story's unit tests use; a
// genuine escape aborts the process directly. This file has no dependency
// on any project assertion macro (none exists in this codebase) — a bare
// `std::abort()` is used deliberately so libFuzzer sees an unambiguous
// crash and keeps the triggering input in its crash corpus, rather than a
// GTest-style report that a non-fuzz harness would just log and continue
// past.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string>

#include "revenant/recovery/OutputPath.hpp"

namespace {

std::string toString(std::span<const std::uint8_t> input) {
	std::string text(input.size(), '\0');
	std::ranges::transform(input, text.begin(), [](std::uint8_t byte) {
		return static_cast<char>(byte);
	});
	return text;
}

const std::filesystem::path& fuzzRoot() {
	static const std::filesystem::path kRoot =
		std::filesystem::temp_directory_path() / "revenant-output-path-fuzz-root";
	return kRoot;
}

void abortIfEscaped(const std::filesystem::path& candidate) {
	if (!candidate.string().starts_with(fuzzRoot().string())) {
		std::abort();
	}
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming) - fixed C-ABI name libFuzzer links against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const std::string name = toString(std::span<const std::uint8_t>{data, size});
	const auto result = revenant::recovery::sanitizeOutputPath(fuzzRoot(), name);
	if (result.hasValue()) {
		abortIfEscaped(result.value());
	}
	return 0;
}

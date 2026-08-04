// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/CliArgs.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <system_error>

#include "imagegen/PatternWriter.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::imagegen {

// std::from_chars's [first, last) pointer-pair signature is the only overload
// portable across our toolchains here (MSVC's checked/debug string_view
// iterator does not interoperate with the const-char*-returning overload);
// the arithmetic below just spans one already-bounded string_view.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
Result<std::uint64_t> parseSize(std::string_view text) noexcept {
	std::uint64_t value = 0;
	const auto [end, err] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (err != std::errc{} || end != text.data() + text.size()) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	return value;
}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

// The index bounds are checked by each caller before it reads: a verb's
// argument count is matched before it runs. std::span has no checked accessor
// (operator[] only) in C++20.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
std::string_view argAt(std::span<char* const> args, std::size_t index) {
	return args[index];
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

Result<GenerateRequest> parsePatternArgs(std::span<char* const> args) {
	const auto size = parseSize(argAt(args, kSizeIndex));
	if (!size.hasValue()) {
		return size.error();
	}
	return parsePattern(argAt(args, kPatternIndex)).map([&](Pattern pattern) {
		return GenerateRequest{
			.outputPath = argAt(args, kOutputIndex),
			.sizeBytes = size.value(),
			.pattern = pattern};
	});
}

} // namespace revenant::imagegen

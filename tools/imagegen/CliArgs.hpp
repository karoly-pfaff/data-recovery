// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

#include "imagegen/PatternWriter.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::imagegen {

// Reading a command line, kept apart from deciding what to do with it. Internal
// to the tool — `CliMain.cpp` is its only consumer — but its own file, because
// "which argument is where and how a number parses" changes for different
// reasons than the verb table does.

// A verb is its name *and* its argument count together, so the counts are part
// of the grammar rather than of any one verb.
inline constexpr std::size_t kPatternArgs = 5; // program, verb, output, size, pattern
inline constexpr std::size_t kPlantedArgs = 5; // program, verb, output, size, plant count
inline constexpr std::size_t kSizedArgs = 4;   // program, verb, output, size
inline constexpr std::size_t kNamedArgs = 3;   // program, verb, output
inline constexpr std::size_t kVerbIndex = 1;
inline constexpr std::size_t kOutputIndex = 2;
inline constexpr std::size_t kSizeIndex = 3;
// The last slot is the verb's own: two verbs share the position, not the role.
inline constexpr std::size_t kPatternIndex = 4;
inline constexpr std::size_t kPlantCountIndex = 4;

struct GenerateRequest {
	std::filesystem::path outputPath;
	std::uint64_t sizeBytes = 0;
	Pattern pattern = Pattern::kZero;
};

// A whole non-negative number, or `kInvalidArgument`. Trailing text is a
// refusal, not a shorter number.
[[nodiscard]] Result<std::uint64_t> parseSize(std::string_view text) noexcept;

// The argument at `index`. Every caller checks the count first — a verb's
// argument count is matched before it runs.
[[nodiscard]] std::string_view argAt(std::span<char* const> args, std::size_t index);

// The `pattern` verb's three operands, or the first refusal among them.
[[nodiscard]] Result<GenerateRequest> parsePatternArgs(std::span<char* const> args);

} // namespace revenant::imagegen

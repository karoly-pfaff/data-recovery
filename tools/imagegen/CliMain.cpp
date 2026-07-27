// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/CliMain.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <system_error>

#include "imagegen/PatternWriter.hpp"
#include "imagegen/ntfs/NtfsImageBuilder.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/log/LogLevel.hpp"
#include "revenant/core/log/Logger.hpp"
#include "revenant/core/log/StderrSink.hpp"

namespace revenant::imagegen {

namespace {

constexpr std::size_t kPatternArgs = 5; // program, verb, output, size, pattern
constexpr std::size_t kNtfsArgs = 3;    // program, verb, output
constexpr std::size_t kVerbIndex = 1;
constexpr std::size_t kOutputIndex = 2;
constexpr std::size_t kSizeIndex = 3;
constexpr std::size_t kPatternIndex = 4;

struct GenerateRequest {
	std::filesystem::path outputPath;
	std::uint64_t sizeBytes = 0;
	Pattern pattern = Pattern::kZero;
};

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

// The index bounds are checked by each caller below before it reads; std::span
// has no checked accessor (operator[] only) in C++20.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
[[nodiscard]] std::string_view argAt(std::span<char* const> args, std::size_t index) {
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

bool reportUsageError(Logger& logger) {
	logger.log(
		LogLevel::kError,
		"usage: revenant-imagegen pattern <output> <size-bytes> <zero|counter|lba>"
		" | revenant-imagegen ntfs <output>");
	return false;
}

bool runPattern(std::span<char* const> args, Logger& logger) {
	const auto request = parsePatternArgs(args);
	if (!request.hasValue()) {
		return reportUsageError(logger);
	}
	if (!writeImage(request.value().outputPath, request.value().sizeBytes, request.value().pattern)
			 .hasValue()) {
		logger.log(LogLevel::kError, "image generation failed while writing");
		return false;
	}
	return true;
}

bool runNtfs(std::span<char* const> args, Logger& logger) {
	if (!ntfs::writeNtfsImage(std::filesystem::path{argAt(args, kOutputIndex)}).hasValue()) {
		logger.log(LogLevel::kError, "NTFS image generation failed while writing");
		return false;
	}
	return true;
}

// Dispatch is by verb *and* argument count together: a verb with the wrong
// number of arguments is a usage error, not a differently-shaped request.
bool dispatch(std::span<char* const> args, Logger& logger) {
	const auto verb = argAt(args, kVerbIndex);
	if (verb == "pattern" && args.size() == kPatternArgs) {
		return runPattern(args, logger);
	}
	if (verb == "ntfs" && args.size() == kNtfsArgs) {
		return runNtfs(args, logger);
	}
	return reportUsageError(logger);
}

} // namespace

bool runCli(std::span<char* const> args) {
	StderrSink sink;
	Logger logger{sink, LogLevel::kInfo};
	if (args.size() <= kVerbIndex) {
		return reportUsageError(logger);
	}
	return dispatch(args, logger);
}

} // namespace revenant::imagegen

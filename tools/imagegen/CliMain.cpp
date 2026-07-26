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
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/log/LogLevel.hpp"
#include "revenant/core/log/Logger.hpp"
#include "revenant/core/log/StderrSink.hpp"

namespace revenant::imagegen {

namespace {

constexpr std::size_t kExpectedArgs = 4; // program, output, size, pattern

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

// `args` is exactly kExpectedArgs long past this point (checked by the caller
// below), so args[1..3] are in range; std::span has no checked accessor
// (operator[] only) in C++20.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
Result<GenerateRequest> parseArgs(std::span<char* const> args) {
	if (args.size() != kExpectedArgs) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	const auto size = parseSize(args[2]);
	if (!size.hasValue()) {
		return size.error();
	}
	return parsePattern(args[3]).map([&](Pattern pattern) {
		return GenerateRequest{
			.outputPath = args[1],
			.sizeBytes = size.value(),
			.pattern = pattern};
	});
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

// Logs the usage message and reports failure; split out of runCli() to keep
// that function under the 10-statement limit.
bool reportUsageError(Logger& logger) {
	logger.log(
		LogLevel::kError,
		"usage: revenant-imagegen <output> <size-bytes> <zero|counter|lba>");
	return false;
}

// Generates the requested image and logs on failure; split out of runCli()
// for the same reason as reportUsageError().
bool generateAndReport(const GenerateRequest& request, Logger& logger) {
	const auto written = writeImage(request.outputPath, request.sizeBytes, request.pattern);
	if (!written.hasValue()) {
		logger.log(LogLevel::kError, "image generation failed while writing");
		return false;
	}
	return true;
}

} // namespace

bool runCli(std::span<char* const> args) {
	StderrSink sink;
	Logger logger{sink, LogLevel::kInfo};
	const auto request = parseArgs(args);
	if (!request.hasValue()) {
		return reportUsageError(logger);
	}
	return generateAndReport(request.value(), logger);
}

} // namespace revenant::imagegen

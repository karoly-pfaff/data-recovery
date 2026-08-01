// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/io/SysfsFields.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "core/SafeArith.hpp"
#include "core/TextNumber.hpp"
#include "core/io/DeviceNumber.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant {

namespace {

constexpr std::uint64_t kSysfsUnitBytes = 512;

} // namespace

std::optional<std::string> sysfsLine(const std::filesystem::path& file) {
	std::ifstream reading{file};
	std::string line;
	if (!std::getline(reading, line)) {
		return std::nullopt;
	}
	return line;
}

std::optional<std::string> sysfsNodeName(std::string_view text) {
	return deviceKeyIn(text).has_value() ? std::optional{std::string{text}} : std::nullopt;
}

std::optional<std::uint64_t> sysfsNumber(const std::filesystem::path& file) {
	const auto text = sysfsLine(file);
	return text.has_value() ? numberIn(text.value()) : std::nullopt;
}

Result<std::uint64_t> sysfsUnitsToBytes(std::optional<std::uint64_t> units) {
	if (!units.has_value()) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	return safeMul64(units.value(), kSysfsUnitBytes, 0);
}

bool sysfsPresent(const std::filesystem::path& path) {
	std::error_code missing;
	return std::filesystem::exists(path, missing);
}

} // namespace revenant

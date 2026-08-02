// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery/SessionFile.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>

#include "recovery/StorageRoom.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::recovery {

namespace {

constexpr std::string_view kPendingSuffix = ".pending";

[[nodiscard]] Result<std::filesystem::path>
putPending(const std::filesystem::path& path, std::string_view text) {
	std::ofstream stream{path, std::ios::binary | std::ios::trunc};
	stream << text;
	stream.flush();
	if (!stream.good()) {
		return Error{.code = writeFailureAt(path), .offset = 0, .osCode = 0};
	}
	return path;
}

[[nodiscard]] Result<std::filesystem::path>
renameOver(const std::filesystem::path& pending, const std::filesystem::path& target) {
	std::error_code failure;
	std::filesystem::rename(pending, target, failure);
	if (failure) {
		return Error{
			.code = ErrorCode::kIoFailure,
			.offset = 0,
			.osCode = static_cast<std::int32_t>(failure.value())};
	}
	return target;
}

} // namespace

Result<std::filesystem::path>
replaceFile(const std::filesystem::path& target, std::string_view text) {
	auto pending = target;
	pending += kPendingSuffix;
	const auto written = putPending(pending, text);
	if (!written.hasValue()) {
		return written.error();
	}
	return renameOver(pending, target);
}

} // namespace revenant::recovery

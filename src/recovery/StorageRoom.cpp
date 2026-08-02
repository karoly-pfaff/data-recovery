// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery/StorageRoom.hpp"

#include <filesystem>
#include <system_error>

#include "revenant/core/Error.hpp"

namespace revenant::recovery {

namespace {

// The directory the write was aimed at. `space` wants somewhere that exists,
// and the file being written may well not — that is one of the ways a write
// fails.
[[nodiscard]] std::filesystem::path directoryOf(const std::filesystem::path& path) {
	std::error_code failure;
	if (std::filesystem::is_directory(path, failure)) {
		return path;
	}
	return path.parent_path();
}

} // namespace

ErrorCode writeFailureAt(const std::filesystem::path& path) {
	std::error_code failure;
	const auto room = std::filesystem::space(directoryOf(path), failure);
	if (failure || room.available > 0) {
		return ErrorCode::kIoFailure;
	}
	return ErrorCode::kStorageExhausted;
}

} // namespace revenant::recovery

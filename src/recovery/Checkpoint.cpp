// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/Checkpoint.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "revenant/core/Endian.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/Sha256.hpp"

namespace revenant::recovery {

namespace {

constexpr std::array<std::byte, 8> kMagic{
	std::byte{'R'},
	std::byte{'V'},
	std::byte{'N'},
	std::byte{'T'},
	std::byte{'C'},
	std::byte{'K'},
	std::byte{'P'},
	std::byte{0x00}};
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kVersionAt = 8;
constexpr std::size_t kShapeAt = 16;
constexpr std::size_t kCursorAt = 48;
constexpr std::size_t kRecordsAt = 56;
constexpr std::string_view kPendingFileName = "checkpoint.new";

void put(std::span<std::byte> raw, std::size_t at, std::span<const std::byte> value) {
	std::ranges::copy(value, raw.subspan(at, value.size()).begin());
}

[[nodiscard]] std::array<std::byte, kCheckpointBytes> encode(const Checkpoint& checkpoint) {
	std::array<std::byte, kCheckpointBytes> raw{};
	put(raw, 0, kMagic);
	put(raw, kVersionAt, toLittleEndian(kVersion));
	put(raw, kShapeAt, checkpoint.shape.bytes);
	put(raw, kCursorAt, toLittleEndian(checkpoint.scanCursor));
	put(raw, kRecordsAt, toLittleEndian(checkpoint.indexRecords));
	return raw;
}

[[nodiscard]] std::uint64_t readAt(std::span<const std::byte> raw, std::size_t at) {
	return fromLittleEndian<std::uint64_t>(
		std::span<const std::byte, sizeof(std::uint64_t)>{raw.subspan(at, sizeof(std::uint64_t))});
}

[[nodiscard]] Sha256Digest shapeIn(std::span<const std::byte> raw) {
	Sha256Digest shape{};
	std::ranges::copy(raw.subspan(kShapeAt, kSha256Bytes), shape.bytes.begin());
	return shape;
}

[[nodiscard]] bool headerIsOurs(std::span<const std::byte> raw) {
	const auto version =
		fromLittleEndian<std::uint32_t>(std::span<const std::byte, sizeof(std::uint32_t)>{
			raw.subspan(kVersionAt, sizeof(std::uint32_t))});
	return std::ranges::equal(raw.first(kMagic.size()), kMagic) && version == kVersion;
}

[[nodiscard]] std::vector<std::byte> readFile(const std::filesystem::path& path) {
	std::ifstream stream{path, std::ios::binary};
	std::vector<std::byte> bytes;
	for (auto value = stream.get(); value != std::char_traits<char>::eof(); value = stream.get()) {
		bytes.push_back(std::bit_cast<std::byte>(static_cast<char>(value)));
	}
	return bytes;
}

// Written beside the checkpoint and renamed over it, so an interrupted
// replacement leaves one whole checkpoint or the other.
[[nodiscard]] Result<std::filesystem::path>
putPending(const std::filesystem::path& path, std::span<const std::byte> raw) {
	std::ofstream stream{path, std::ios::binary | std::ios::trunc};
	for (const std::byte value : raw) {
		stream.put(std::bit_cast<char>(value));
	}
	stream.flush();
	if (!stream.good()) {
		return Error{.code = ErrorCode::kIoFailure, .offset = 0, .osCode = 0};
	}
	return path;
}

// The rename that makes the replacement whole-or-nothing.
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
writeCheckpoint(const std::filesystem::path& directory, const Checkpoint& checkpoint) {
	const auto pending = directory / kPendingFileName;
	const auto written = putPending(pending, encode(checkpoint));
	if (!written.hasValue()) {
		return written.error();
	}
	return renameOver(pending, directory / kCheckpointFileName);
}

Result<Checkpoint> readCheckpoint(const std::filesystem::path& directory) {
	const auto path = directory / kCheckpointFileName;
	if (!std::filesystem::exists(path)) {
		return Error{.code = ErrorCode::kNotFound, .offset = 0, .osCode = 0};
	}
	const auto raw = readFile(path);
	if (raw.size() != kCheckpointBytes || !headerIsOurs(raw)) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = 0, .osCode = 0};
	}
	return Checkpoint{
		.shape = shapeIn(raw),
		.scanCursor = readAt(raw, kCursorAt),
		.indexRecords = readAt(raw, kRecordsAt)};
}

void clearCheckpoint(const std::filesystem::path& directory) {
	std::error_code ignored;
	std::filesystem::remove(directory / kCheckpointFileName, ignored);
	std::filesystem::remove(directory / kPendingFileName, ignored);
}

} // namespace revenant::recovery

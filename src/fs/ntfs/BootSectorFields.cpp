// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "BootSectorInternal.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/SafeArith.hpp"

namespace revenant::fs::ntfs {

namespace {

constexpr std::array<std::byte, 8> kOemId{
	std::byte{'N'},
	std::byte{'T'},
	std::byte{'F'},
	std::byte{'S'},
	std::byte{' '},
	std::byte{' '},
	std::byte{' '},
	std::byte{' '}};

[[nodiscard]] std::int8_t toSigned(std::uint8_t value) noexcept {
	return std::bit_cast<std::int8_t>(value);
}

[[nodiscard]] Result<std::uint32_t>
recordSizeFromClusters(std::int8_t value, std::uint32_t bytesPerCluster) {
	return safeMul32(static_cast<std::uint32_t>(value), bytesPerCluster, 0x40)
		.andThen([&](std::uint32_t recordBytes) {
			if (recordBytes < 256U || recordBytes > 65536U) {
				return Result<std::uint32_t>(
					Error{.code = ErrorCode::kInvalidArgument, .offset = 0x40});
			}
			return Result<std::uint32_t>(recordBytes);
		});
}

[[nodiscard]] Result<std::uint32_t> recordSizeFromLog2(std::int8_t value) {
	const auto shift = -value;
	if (shift < 8 || shift > 16) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = 0x40};
	}
	return std::uint32_t{1} << static_cast<std::uint32_t>(shift);
}

[[nodiscard]] Result<std::uint32_t>
recordSizeFromRaw(std::int8_t value, std::uint32_t bytesPerCluster) {
	if (value > 0) {
		return recordSizeFromClusters(value, bytesPerCluster);
	}
	if (value < 0) {
		return recordSizeFromLog2(value);
	}
	return Error{.code = ErrorCode::kInvalidArgument, .offset = 0x40};
}

} // namespace

Result<bool> oemIdIsValid(const ByteReader& reader) {
	return reader.bytes(0x03, 8).andThen([](std::span<const std::byte> raw) {
		if (!std::ranges::equal(raw, kOemId)) {
			return Result<bool>(Error{.code = ErrorCode::kInvalidArgument, .offset = 0x03});
		}
		return Result<bool>(true);
	});
}

Result<std::uint64_t> totalSectors(const ByteReader& reader) {
	return reader.readLe<std::uint64_t>(0x28).andThen([](std::uint64_t raw) {
		if (raw == 0U) {
			return Result<std::uint64_t>(
				Error{.code = ErrorCode::kInvalidArgument, .offset = 0x28});
		}
		return Result<std::uint64_t>(raw);
	});
}

Result<std::uint64_t> mftClusterNumber(const ByteReader& reader) {
	return reader.readLe<std::uint64_t>(0x30);
}

Result<std::uint32_t> mftRecordSize(const ByteReader& reader, std::uint32_t bytesPerCluster) {
	return reader.bytes(0x40, 1).andThen([&](std::span<const std::byte> raw) {
		const auto value = toSigned(std::to_integer<std::uint8_t>(raw.front()));
		return recordSizeFromRaw(value, bytesPerCluster);
	});
}

} // namespace revenant::fs::ntfs

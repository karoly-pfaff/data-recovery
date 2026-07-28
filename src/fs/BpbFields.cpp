// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/BpbFields.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs {

namespace {

constexpr std::uint32_t kMaxSectorsPerCluster = 128;

[[nodiscard]] bool isPowerOfTwo(std::uint32_t value) noexcept {
	return value != 0 && (value & (value - 1)) == 0;
}

[[nodiscard]] bool isKnownSectorSize(std::uint32_t value) noexcept {
	return value == 512U || value == 1024U || value == 2048U || value == 4096U;
}

} // namespace

Result<std::uint32_t> bytesPerSector(const ByteReader& reader) {
	return reader.readLe<std::uint16_t>(kBytesPerSectorOffset).andThen([](std::uint16_t raw) {
		const auto value = static_cast<std::uint32_t>(raw);
		if (!isKnownSectorSize(value)) {
			return Result<std::uint32_t>(
				Error{.code = ErrorCode::kInvalidArgument, .offset = kBytesPerSectorOffset});
		}
		return Result<std::uint32_t>(value);
	});
}

Result<std::uint32_t> sectorsPerCluster(const ByteReader& reader) {
	return reader.bytes(kSectorsPerClusterOffset, 1).andThen([](std::span<const std::byte> raw) {
		const auto value = std::to_integer<std::uint32_t>(raw.front());
		if (!isPowerOfTwo(value) || value > kMaxSectorsPerCluster) {
			return Result<std::uint32_t>(
				Error{.code = ErrorCode::kInvalidArgument, .offset = kSectorsPerClusterOffset});
		}
		return Result<std::uint32_t>(value);
	});
}

Result<bool> bootSignatureIsValid(const ByteReader& reader) {
	return reader.bytes(kBootSignatureOffset, 2).andThen([](std::span<const std::byte> raw) {
		if (raw.front() != std::byte{0x55} || raw.back() != std::byte{0xAA}) {
			return Result<bool>(
				Error{.code = ErrorCode::kInvalidArgument, .offset = kBootSignatureOffset});
		}
		return Result<bool>(true);
	});
}

} // namespace revenant::fs

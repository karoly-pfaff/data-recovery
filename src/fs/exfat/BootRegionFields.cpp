// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "BootRegionInternal.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::exfat {

namespace {

constexpr std::uint64_t kSignatureOffset = 0x1FE;

// The 53 bytes a FAT BPB keeps its geometry in. exFAT zeroes every one of them
// on purpose, so that a FAT driver reading this volume sees nonsense and stops
// rather than mounting it wrongly.
constexpr std::size_t kMustBeZeroBytes = 53;

constexpr std::array<std::byte, 8> kFileSystemName{
	std::byte{'E'},
	std::byte{'X'},
	std::byte{'F'},
	std::byte{'A'},
	std::byte{'T'},
	std::byte{' '},
	std::byte{' '},
	std::byte{' '}};

// A sector is 512 to 4096 bytes, stated as its log2.
constexpr std::uint32_t kMinSectorShift = 9;
constexpr std::uint32_t kMaxSectorShift = 12;
// A cluster may not exceed 32 MiB, which the format states as a cap on the sum
// of the two shifts.
constexpr std::uint32_t kMaxClusterShiftSum = 25;

[[nodiscard]] Result<bool> nameMatches(const ByteReader& reader) {
	return reader.bytes(kFileSystemNameOffset, kFileSystemName.size())
		.andThen([](std::span<const std::byte> raw) {
			if (!std::ranges::equal(raw, kFileSystemName)) {
				return Result<bool>(
					Error{.code = ErrorCode::kInvalidArgument, .offset = kFileSystemNameOffset});
			}
			return Result<bool>(true);
		});
}

[[nodiscard]] Result<bool> mustBeZeroIsZero(const ByteReader& reader) {
	return reader.bytes(kMustBeZeroOffset, kMustBeZeroBytes)
		.andThen([](std::span<const std::byte> raw) {
			if (std::ranges::any_of(raw, [](std::byte value) { return value != std::byte{0}; })) {
				return Result<bool>(
					Error{.code = ErrorCode::kInvalidArgument, .offset = kMustBeZeroOffset});
			}
			return Result<bool>(true);
		});
}

[[nodiscard]] Result<std::uint32_t> shiftAt(const ByteReader& reader, std::uint64_t offset) {
	return reader.bytes(offset, 1).map(
		[](std::span<const std::byte> raw) { return std::to_integer<std::uint32_t>(raw.front()); });
}

} // namespace

Result<bool> namesExfat(const ByteReader& reader) {
	return nameMatches(reader).andThen([&reader](bool) { return mustBeZeroIsZero(reader); });
}

Result<std::uint32_t> bytesPerSector(const ByteReader& reader) {
	return shiftAt(reader, kSectorShiftOffset).andThen([](std::uint32_t shift) {
		if (shift < kMinSectorShift || shift > kMaxSectorShift) {
			return Result<std::uint32_t>(
				Error{.code = ErrorCode::kInvalidArgument, .offset = kSectorShiftOffset});
		}
		return Result<std::uint32_t>(std::uint32_t{1} << shift);
	});
}

Result<std::uint32_t> sectorsPerCluster(const ByteReader& reader, std::uint32_t sectorBytes) {
	const auto sectorShift = static_cast<std::uint32_t>(std::countr_zero(sectorBytes));
	return shiftAt(reader, kClusterShiftOffset).andThen([sectorShift](std::uint32_t shift) {
		if (shift + sectorShift > kMaxClusterShiftSum) {
			return Result<std::uint32_t>(
				Error{.code = ErrorCode::kInvalidArgument, .offset = kClusterShiftOffset});
		}
		return Result<std::uint32_t>(std::uint32_t{1} << shift);
	});
}

Result<std::uint32_t> fatCount(const ByteReader& reader) {
	return reader.bytes(kFatCountOffset, 1).andThen([](std::span<const std::byte> raw) {
		const auto value = std::to_integer<std::uint32_t>(raw.front());
		if (value != 1U && value != 2U) {
			return Result<std::uint32_t>(
				Error{.code = ErrorCode::kInvalidArgument, .offset = kFatCountOffset});
		}
		return Result<std::uint32_t>(value);
	});
}

Result<bool> signatureIsValid(const ByteReader& reader) {
	return reader.bytes(kSignatureOffset, 2).andThen([](std::span<const std::byte> raw) {
		if (raw.front() != std::byte{0x55} || raw.back() != std::byte{0xAA}) {
			return Result<bool>(
				Error{.code = ErrorCode::kInvalidArgument, .offset = kSignatureOffset});
		}
		return Result<bool>(true);
	});
}

} // namespace revenant::fs::exfat

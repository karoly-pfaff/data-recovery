// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "BootSectorInternal.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::fat {

namespace {

constexpr std::array<std::byte, 8> kFat32Type{
	std::byte{'F'},
	std::byte{'A'},
	std::byte{'T'},
	std::byte{'3'},
	std::byte{'2'},
	std::byte{' '},
	std::byte{' '},
	std::byte{' '}};

// Root entry count, 16-bit total sectors, 16-bit FAT size. A FAT32 volume
// carries its counterparts in the 32-bit fields and leaves all three at zero;
// a non-zero one means this is FAT12/16 wearing a FAT32 label.
constexpr std::array<std::uint64_t, 3> kFat16OnlyOffsets{0x11, 0x13, 0x16};

// A field that must not be zero, rejected at its own offset when it is.
[[nodiscard]] Result<std::uint64_t> nonZero(Result<std::uint64_t> field, std::uint64_t offset) {
	if (field.hasValue() && field.value() == 0U) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = offset};
	}
	return field;
}

// One of the FAT12/16-only fields, judged on its own so the walk over them
// stays a walk.
[[nodiscard]] Result<bool> fieldIsZero(const ByteReader& reader, std::uint64_t offset) {
	return reader.readLe<std::uint16_t>(offset).andThen([offset](std::uint16_t raw) {
		if (raw != 0U) {
			return Result<bool>(Error{.code = ErrorCode::kInvalidArgument, .offset = offset});
		}
		return Result<bool>(true);
	});
}

[[nodiscard]] Result<std::uint64_t> readWide(const ByteReader& reader, std::uint64_t offset) {
	return reader.readLe<std::uint32_t>(offset).map(
		[](std::uint32_t raw) { return static_cast<std::uint64_t>(raw); });
}

} // namespace

Result<bool> filSysTypeIsFat32(const ByteReader& reader) {
	return reader.bytes(kFilSysTypeOffset, kFat32Type.size())
		.andThen([](std::span<const std::byte> raw) {
			if (!std::ranges::equal(raw, kFat32Type)) {
				return Result<bool>(
					Error{.code = ErrorCode::kInvalidArgument, .offset = kFilSysTypeOffset});
			}
			return Result<bool>(true);
		});
}

Result<bool> fat16OnlyFieldsAreZero(const ByteReader& reader) {
	for (const std::uint64_t offset : kFat16OnlyOffsets) {
		const auto field = fieldIsZero(reader, offset);
		if (!field.hasValue()) {
			return field;
		}
	}
	return true;
}

Result<std::uint32_t> reservedSectors(const ByteReader& reader) {
	return reader.readLe<std::uint16_t>(kReservedSectorsOffset).andThen([](std::uint16_t raw) {
		if (raw == 0U) {
			return Result<std::uint32_t>(
				Error{.code = ErrorCode::kInvalidArgument, .offset = kReservedSectorsOffset});
		}
		return Result<std::uint32_t>(static_cast<std::uint32_t>(raw));
	});
}

Result<std::uint32_t> fatCount(const ByteReader& reader) {
	return reader.bytes(kFatCountOffset, 1).andThen([](std::span<const std::byte> raw) {
		const auto value = std::to_integer<std::uint32_t>(raw.front());
		if (value == 0U) {
			return Result<std::uint32_t>(
				Error{.code = ErrorCode::kInvalidArgument, .offset = kFatCountOffset});
		}
		return Result<std::uint32_t>(value);
	});
}

Result<std::uint64_t> fatSectors(const ByteReader& reader) {
	return nonZero(readWide(reader, kFatSizeOffset), kFatSizeOffset);
}

Result<std::uint64_t> totalSectors(const ByteReader& reader) {
	return nonZero(readWide(reader, kTotalSectorsOffset), kTotalSectorsOffset);
}

Result<std::uint32_t> rootCluster(const ByteReader& reader) {
	return reader.readLe<std::uint32_t>(kRootClusterOffset);
}

} // namespace revenant::fs::fat

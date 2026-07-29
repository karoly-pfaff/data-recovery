// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <cstdint>
#include <vector>

#include "revenant/core/Crc32.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/volume/Gpt.hpp"
#include "volume/GptInternal.hpp"
#include "volume/GptLayout.hpp"
#include "volume/SectorIo.hpp"

namespace revenant::volume {

namespace {

// Where the entry array is and how much of it there is. Named rather than passed
// as two adjacent integers, because an offset and a length are exactly the pair
// a caller can swap without the compiler noticing.
struct ArrayExtent {
	std::uint64_t offset = 0;
	std::size_t bytes = 0;
};

// The header already bounded this product against kMaxEntryArrayBytes, so it
// cannot overflow the size type it is computed in.
[[nodiscard]] std::size_t arrayBytesOf(const GptHeader& header) {
	return static_cast<std::size_t>(header.entryCount) * header.entryBytes;
}

[[nodiscard]] Result<std::vector<std::byte>>
arrayAt(BlockDevice& device, const ArrayExtent& extent) {
	std::vector<std::byte> array(extent.bytes);
	const auto read = device.readAt(extent.offset, array);
	if (!read.hasValue()) {
		return read.error();
	}
	if (read.value() < array.size()) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = extent.offset};
	}
	return array;
}

// The array is only worth reading if it is the array the header signed for. A
// mismatch is this copy of the table failing, which is what sends the read to
// the other copy.
[[nodiscard]] Result<std::vector<std::byte>>
checksummed(std::vector<std::byte> array, const GptHeader& header) {
	if (crc32(array) != header.entryArrayCrc) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = kEntryArrayCrcOffset};
	}
	return array;
}

} // namespace

Result<std::vector<std::byte>> readEntryArray(BlockDevice& device, const GptHeader& header) {
	return byteOffsetOf(device, header.entryArrayLba)
		.andThen([&](std::uint64_t offset) {
			return arrayAt(device, ArrayExtent{.offset = offset, .bytes = arrayBytesOf(header)});
		})
		.andThen(
			[&header](const std::vector<std::byte>& array) { return checksummed(array, header); });
}

} // namespace revenant::volume

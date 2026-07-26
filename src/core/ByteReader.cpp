// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/ByteReader.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant {

Result<std::span<const std::byte>>
ByteReader::bytes(std::uint64_t offset, std::size_t count) const noexcept {
	if (offset > data_.size()) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = offset};
	}
	if (count > data_.size() - offset) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = offset};
	}
	return data_.subspan(static_cast<std::size_t>(offset), count);
}

} // namespace revenant

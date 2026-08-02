// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace revenant {

// A range of a device that could not be read, and was handed back as zeros
// instead.
//
// It has a header of its own because it travels: the I/O layer produces it, the
// recovery layer intersects it with an artifact's extents, and the manifest
// prints it. A record type should not have to include a device class to say
// which of its bytes were invented.
struct BadRange {
	std::uint64_t offsetBytes = 0;
	std::uint64_t lengthBytes = 0;

	friend bool operator==(const BadRange&, const BadRange&) = default;
};

} // namespace revenant

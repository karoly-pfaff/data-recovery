// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/io/NativeSourceDevice.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/ReadRange.hpp"

// The two bodies every device backed by an OS handle would otherwise have
// written for itself. Living in this single, unconditionally-compiled
// translation unit — rather than as non-inline definitions in the shared header
// — keeps them ODR-safe: any TU may include ReadRange.hpp (a unit test for
// clampReadRange does) without risking a duplicate-symbol link error.

namespace revenant {

NativeSourceDevice::~NativeSourceDevice() {
	closeNative(nativeHandle_);
}

Result<std::size_t>
NativeSourceDevice::readHandle(std::uint64_t offset, std::span<std::byte> buffer) const {
	return readNative(nativeHandle_, offset, buffer);
}

} // namespace revenant

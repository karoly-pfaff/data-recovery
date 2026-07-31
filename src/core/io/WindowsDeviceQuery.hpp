// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace revenant {

// One fixed-size `DeviceIoControl` that fills `into`. Its buffers are `void*`,
// so a caller hands it the bytes of a Win32 struct rather than the struct.
//
// The handle arrives as the `std::intptr_t` the I/O layer already carries
// native handles in, and the control code as a plain number, so this header
// stays free of `windows.h` — which is what lets both the device measuring and
// the identity resolution share one answer to "ask this device a question".
[[nodiscard]] bool
queryDevice(std::intptr_t nativeHandle, std::uint32_t code, std::span<std::byte> into);

} // namespace revenant

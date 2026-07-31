// SPDX-License-Identifier: GPL-3.0-or-later
// NOLINTBEGIN(misc-include-cleaner) - windows.h is the single correct include
// for the Win32 API surface below. Its internal constituent headers are not
// meant to be included standalone, so misc-include-cleaner cannot resolve a
// "direct" header for these symbols.
#include "core/io/WindowsDeviceQuery.hpp"

#include <windows.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Error.hpp"

namespace revenant {

Error lastWin32Failure() {
	return Error{
		.code = ErrorCode::kIoFailure,
		.offset = 0,
		.osCode = static_cast<std::int32_t>(::GetLastError())};
}

bool queryDevice(std::intptr_t nativeHandle, std::uint32_t code, std::span<std::byte> into) {
	DWORD returned = 0;
	return ::DeviceIoControl(
			   std::bit_cast<HANDLE>(nativeHandle),
			   static_cast<DWORD>(code),
			   nullptr,
			   0,
			   into.data(),
			   static_cast<DWORD>(into.size()),
			   &returned,
			   nullptr) != 0;
}

} // namespace revenant

// NOLINTEND(misc-include-cleaner)

// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/io/SourceDevice.hpp"

#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/core/io/ImageFileDevice.hpp"
#include "revenant/core/io/RawDevice.hpp"

namespace revenant {

namespace {

// The concrete device, widened to the seam everything above reads through. A
// unique_ptr cannot be copied out of a Result, so this moves rather than maps.
template <typename Device>
[[nodiscard]] Result<std::unique_ptr<BlockDevice>>
asBlockDevice(Result<std::unique_ptr<Device>> opened) {
	if (!opened.hasValue()) {
		return opened.error();
	}
	return std::unique_ptr<BlockDevice>{std::move(opened.value())};
}

// A path that cannot be interrogated at all is not a regular file, which sends
// the open to the device branch — where the OS gets to give its own reason.
[[nodiscard]] bool isRegularFile(const std::filesystem::path& source) {
	std::error_code failure;
	return std::filesystem::is_regular_file(source, failure);
}

} // namespace

Result<std::unique_ptr<BlockDevice>> openSource(const std::filesystem::path& source) {
	if (isRegularFile(source)) {
		return asBlockDevice(ImageFileDevice::open(source));
	}
	return asBlockDevice(RawDevice::open(source));
}

} // namespace revenant

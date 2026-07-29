// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/io/SourceDevice.hpp"

#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

#include "revenant/core/Error.hpp"
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

// A share root, a mounted NFS or SMB path, and a plain folder are all
// directories, and all fail for the same reason (ADR-0007): what they expose is
// a filesystem's answer about *live* files, not the bytes a filesystem was
// written into. One check covers every spelling of the mistake without this
// layer learning what a UNC path looks like.
[[nodiscard]] bool isDirectory(const std::filesystem::path& source) {
	std::error_code failure;
	return std::filesystem::is_directory(source, failure);
}

} // namespace

Result<std::unique_ptr<BlockDevice>> openSource(const std::filesystem::path& source) {
	if (isDirectory(source)) {
		return Error{.code = ErrorCode::kNotBlockAddressable};
	}
	if (isRegularFile(source)) {
		return asBlockDevice(ImageFileDevice::open(source));
	}
	return asBlockDevice(RawDevice::open(source));
}

} // namespace revenant

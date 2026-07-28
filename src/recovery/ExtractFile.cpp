// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery/ExtractFile.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <system_error>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/recovery/Candidate.hpp"
#include "revenant/recovery/RecoverySink.hpp"

namespace revenant::recovery {

namespace {

// Streamed through an output iterator rather than a laundered pointer: no
// reinterpret_cast, and the copy is already bounded by its caller.
void putBytes(std::ofstream& out, std::span<const std::byte> raw) {
	std::ranges::transform(raw, std::ostreambuf_iterator<char>{out}, [](std::byte value) {
		return std::bit_cast<char>(value);
	});
}

// One bounded chunk of an extent: read whole, or a typed error. A short read
// means the device does not hold what the metadata claimed, which is a failed
// recovery — never a shorter file that looks complete.
[[nodiscard]] Result<std::uint64_t>
copyChunk(std::ofstream& out, BlockDevice& device, std::uint64_t at, std::span<std::byte> chunk) {
	const auto read = device.readAt(at, chunk);
	if (!read.hasValue()) {
		return read.error();
	}
	if (read.value() != chunk.size()) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = at};
	}
	putBytes(out, chunk);
	return chunk.size();
}

// Where an extent copy has got to: how many of its bytes are down, or the
// error that stopped it.
[[nodiscard]] Result<std::uint64_t> advanceExtent(
	std::ofstream& out,
	BlockDevice& device,
	const fs::Extent& extent,
	std::span<std::byte> scratch) {
	Result<std::uint64_t> done = std::uint64_t{0};
	while (done.hasValue() && done.value() < extent.lengthBytes) {
		const auto want =
			std::min<std::uint64_t>(scratch.size(), extent.lengthBytes - done.value());
		const auto at = extent.deviceOffset + done.value();
		const auto copied =
			copyChunk(out, device, at, scratch.first(static_cast<std::size_t>(want)));
		done = copied.hasValue() ? Result<std::uint64_t>{done.value() + copied.value()} : copied;
	}
	return done;
}

// The running total after one more extent, or the error that ended the copy.
[[nodiscard]] Result<std::uint64_t>
plusExtent(Result<std::uint64_t> total, Result<std::uint64_t> copied) {
	if (!total.hasValue() || !copied.hasValue()) {
		return copied.hasValue() ? total : copied;
	}
	return total.value() + copied.value();
}

[[nodiscard]] Result<std::uint64_t>
copyExtents(std::ofstream& out, BlockDevice& device, const Candidate& winner) {
	std::vector<std::byte> scratch(kExtractChunkBytes, std::byte{0});
	Result<std::uint64_t> total = std::uint64_t{0};
	for (const fs::Extent& extent : winner.extents) {
		total = plusExtent(total, advanceExtent(out, device, extent, scratch));
	}
	return total;
}

// A stream that went bad after the last write took something with it, so the
// byte count is only trustworthy once the stream is.
[[nodiscard]] Result<std::uint64_t> flushed(std::ofstream& out, std::uint64_t written) {
	out.flush();
	if (!out.good()) {
		return Error{.code = ErrorCode::kIoFailure, .offset = written};
	}
	return written;
}

[[nodiscard]] Result<std::uint64_t>
writeContent(std::ofstream& out, const Candidate& winner, BlockDevice& device) {
	if (winner.extents.empty()) {
		putBytes(out, winner.residentContent);
		return flushed(out, winner.residentContent.size());
	}
	const auto copied = copyExtents(out, device, winner);
	return copied.hasValue() ? flushed(out, copied.value()) : copied;
}

} // namespace

Result<std::uint64_t>
extractTo(const std::filesystem::path& target, const Candidate& winner, BlockDevice& device) {
	std::error_code ignored;
	std::filesystem::create_directories(target.parent_path(), ignored);
	std::ofstream out{target, std::ios::binary | std::ios::trunc};
	if (!out.good()) {
		return Error{.code = ErrorCode::kIoFailure};
	}
	return writeContent(out, winner, device);
}

} // namespace revenant::recovery

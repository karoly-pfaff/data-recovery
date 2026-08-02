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
#include "revenant/core/Sha256.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/recovery/Candidate.hpp"
#include "revenant/recovery/RecoverySink.hpp"

namespace revenant::recovery {

namespace {

// Where an artifact's bytes go: the file being written, and the digest being
// taken of them on the way past. One place, so nothing can be written without
// being hashed.
class Output {
public:
	Output(std::ofstream& file, Sha256& hash) noexcept : file_(&file), hash_(&hash) {}

	// Streamed through an output iterator rather than a laundered pointer: no
	// reinterpret_cast, and the copy is already bounded by its caller.
	void put(std::span<const std::byte> raw) {
		hash_->update(raw);
		std::ranges::transform(raw, std::ostreambuf_iterator<char>{*file_}, [](std::byte value) {
			return std::bit_cast<char>(value);
		});
	}

	void flush() {
		file_->flush();
	}

	[[nodiscard]] bool good() const {
		return file_->good();
	}

private:
	std::ofstream* file_;
	Sha256* hash_;
};

// One bounded chunk of an extent: read whole, or a typed error. A short read
// means the device does not hold what the metadata claimed, which is a failed
// recovery — never a shorter file that looks complete.
[[nodiscard]] Result<std::uint64_t>
copyChunk(Output& out, BlockDevice& device, std::uint64_t at, std::span<std::byte> chunk) {
	const auto read = device.readAt(at, chunk);
	if (!read.hasValue()) {
		return read.error();
	}
	if (read.value() != chunk.size()) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = at};
	}
	out.put(chunk);
	return chunk.size();
}

// Where an extent copy has got to: how many of its bytes are down, or the
// error that stopped it.
[[nodiscard]] Result<std::uint64_t> advanceExtent(
	Output& out,
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
copyExtents(Output& out, BlockDevice& device, const Candidate& winner) {
	std::vector<std::byte> scratch(kExtractChunkBytes, std::byte{0});
	Result<std::uint64_t> total = std::uint64_t{0};
	for (const fs::Extent& extent : winner.extents) {
		total = plusExtent(total, advanceExtent(out, device, extent, scratch));
	}
	return total;
}

// Whether a write failure is the destination running out of room.
//
// The stream itself will not say — `std::ofstream` reports only "bad" — so the
// question is put to the filesystem instead, at the moment of the failure. It
// is worth asking because exhausted storage is the one write failure an
// operator can act on, and because every further write against it is known
// futile: the run stops rather than grinding through the rest of the winner set
// (story-0605).
[[nodiscard]] bool noRoomAt(const std::filesystem::path& target) {
	std::error_code failure;
	const auto room = std::filesystem::space(target.parent_path(), failure);
	return !failure && room.available == 0;
}

[[nodiscard]] ErrorCode writeFailureAt(const std::filesystem::path& target) {
	return noRoomAt(target) ? ErrorCode::kStorageExhausted : ErrorCode::kIoFailure;
}

// A stream that went bad after the last write took something with it, so the
// byte count is only trustworthy once the stream is.
[[nodiscard]] Result<std::uint64_t>
flushed(Output& out, std::uint64_t written, const std::filesystem::path& target) {
	out.flush();
	if (!out.good()) {
		return Error{.code = writeFailureAt(target), .offset = written};
	}
	return written;
}

[[nodiscard]] Result<std::uint64_t> copyInto(
	Output& out,
	const Candidate& winner,
	BlockDevice& device,
	const std::filesystem::path& target) {
	if (winner.extents.empty()) {
		out.put(winner.residentContent);
		return flushed(out, winner.residentContent.size(), target);
	}
	const auto copied = copyExtents(out, device, winner);
	return copied.hasValue() ? flushed(out, copied.value(), target) : copied;
}

[[nodiscard]] Result<ExtractedFile> writeContent(
	std::ofstream& file,
	const Candidate& winner,
	BlockDevice& device,
	const std::filesystem::path& target) {
	Sha256 hash;
	Output out{file, hash};
	const auto written = copyInto(out, winner, device, target);
	if (!written.hasValue()) {
		return written.error();
	}
	return ExtractedFile{.bytes = written.value(), .content = hash.finish()};
}

} // namespace

Result<ExtractedFile>
extractTo(const std::filesystem::path& target, const Candidate& winner, BlockDevice& device) {
	std::error_code ignored;
	std::filesystem::create_directories(target.parent_path(), ignored);
	std::ofstream file{target, std::ios::binary | std::ios::trunc};
	if (!file.good()) {
		return Error{.code = writeFailureAt(target)};
	}
	return writeContent(file, winner, device, target);
}

} // namespace revenant::recovery

// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/ntfs/Runlist.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "RunlistInternal.hpp"
#include "revenant/core/BoundedCount.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::ntfs {

namespace {

constexpr std::uint8_t kEndMarker = 0x00U;

// Where the decoder is: the byte cursor, and the LCN the next run's delta is
// measured from.
struct DecodeState {
	std::size_t cursor;
	std::int64_t previousLcn;
};

enum class Step : std::uint8_t { kRunAppended, kEndReached };

// Widens the run's two's-complement offset field to a full signed delta. The
// width is 1..8 whenever this is called, so the shift pair stays defined.
[[nodiscard]] std::int64_t signedDelta(const RawRun& raw) noexcept {
	const auto shift = (sizeof(std::uint64_t) - raw.offsetWidth) * kBitsPerByte;
	return static_cast<std::int64_t>(raw.rawOffset << shift) >> shift;
}

// `previousLcn` is never negative, so only the positive side can overflow.
[[nodiscard]] Result<std::int64_t>
nextLcn(std::int64_t previousLcn, const RawRun& raw, std::uint64_t offset) {
	const auto delta = signedDelta(raw);
	if (delta > 0 && previousLcn > std::numeric_limits<std::int64_t>::max() - delta) {
		return Error{.code = ErrorCode::kOverflow, .offset = offset};
	}
	const auto next = previousLcn + delta;
	if (next < 0) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = offset};
	}
	return next;
}

[[nodiscard]] Result<DataRun>
placeRun(const RawRun& raw, std::int64_t previousLcn, std::uint64_t offset) {
	if (raw.offsetWidth == 0) {
		return DataRun{.startCluster = 0, .lengthClusters = raw.lengthClusters, .sparse = true};
	}
	return nextLcn(previousLcn, raw, offset).map([&raw](std::int64_t lcn) {
		return DataRun{
			.startCluster = static_cast<std::uint64_t>(lcn),
			.lengthClusters = raw.lengthClusters,
			.sparse = false};
	});
}

// A sparse run is backed by no clusters, so it leaves the LCN cursor where it
// was: the next run's delta is measured from the last allocated run.
[[nodiscard]] std::int64_t lcnAfter(const DataRun& run, std::int64_t previousLcn) noexcept {
	return run.sparse ? previousLcn : static_cast<std::int64_t>(run.startCluster);
}

// Both bounded-allocation guards on the accumulating runlist, in one place.
[[nodiscard]] Result<std::uint64_t>
checkedTotal(const Runlist& runlist, const DataRun& run, std::uint64_t offset) {
	if (!boundedCount(runlist.runs.size() + 1U, kMaxDataRuns).hasValue()) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = offset};
	}
	if (run.lengthClusters > std::numeric_limits<std::uint64_t>::max() - runlist.totalClusters) {
		return Error{.code = ErrorCode::kOverflow, .offset = offset};
	}
	return runlist.totalClusters + run.lengthClusters;
}

[[nodiscard]] Result<Step>
storeRun(const DataRun& run, std::size_t encodedSize, DecodeState& state, Runlist& runlist) {
	const auto total = checkedTotal(runlist, run, state.cursor);
	if (!total.hasValue()) {
		return total.error();
	}
	runlist.totalClusters = total.value();
	runlist.runs.push_back(run);
	state.previousLcn = lcnAfter(run, state.previousLcn);
	state.cursor += encodedSize;
	return Step::kRunAppended;
}

[[nodiscard]] Result<Step> appendRun(const RawRun& raw, DecodeState& state, Runlist& runlist) {
	const auto run = placeRun(raw, state.previousLcn, state.cursor);
	if (!run.hasValue()) {
		return run.error();
	}
	return storeRun(run.value(), raw.encodedSize, state, runlist);
}

// The end marker is a bare zero byte, so the byte under the cursor is what says
// whether another run follows.
[[nodiscard]] Result<Step> classifyCursor(std::span<const std::byte> bytes, std::size_t cursor) {
	const ByteReader reader{bytes};
	return reader.readLe<std::uint8_t>(cursor).map([](std::uint8_t header) {
		return header == kEndMarker ? Step::kEndReached : Step::kRunAppended;
	});
}

[[nodiscard]] Result<Step>
stepOneRun(std::span<const std::byte> bytes, DecodeState& state, Runlist& runlist) {
	const auto step = classifyCursor(bytes, state.cursor);
	// Running out of bytes and reaching the marker both end the walk.
	if (!step.hasValue() || step.value() == Step::kEndReached) {
		return step;
	}
	const auto raw = readRawRun(bytes, state.cursor);
	if (!raw.hasValue()) {
		return raw.error();
	}
	return appendRun(raw.value(), state, runlist);
}

} // namespace

Result<Runlist> decodeRunlist(std::span<const std::byte> runlistBytes) {
	Runlist runlist;
	DecodeState state{.cursor = 0, .previousLcn = 0};
	Result<Step> step = Step::kRunAppended;
	while (step.hasValue() && step.value() == Step::kRunAppended) {
		step = stepOneRun(runlistBytes, state, runlist);
	}
	if (!step.hasValue()) {
		return step.error();
	}
	return runlist;
}

} // namespace revenant::fs::ntfs

// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/RunScope.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/volume/PartitionTable.hpp"
#include "revenant/volume/PartitionView.hpp"

namespace revenant::recovery {

namespace {

// Zero is not a partition number, which is what leaves it free to mean the
// source itself.
constexpr std::uint32_t kWholeSource = 0;

// The entry an operator asked for, by the number the listing gave it.
[[nodiscard]] Result<volume::Partition>
numbered(const volume::PartitionTable& table, std::uint32_t number) {
	for (const volume::Partition& one : table.partitions) {
		if (one.number == number) {
			return one;
		}
	}
	return Error{.code = ErrorCode::kNotFound};
}

// What a whole-source run walks. A table that will not read and a table that
// names nothing come to the same answer — one volume filling the device, which
// is what an image of a single volume is — and this is the only place holding
// the table that the decision is about.
[[nodiscard]] std::vector<volume::Partition> layoutOf(const Result<volume::PartitionTable>& table) {
	if (!table.hasValue()) {
		return {};
	}
	return table.value().partitions;
}

} // namespace

RunScope::RunScope(
	BlockDevice& device,
	std::unique_ptr<volume::PartitionView> window,
	std::vector<volume::Partition> layout,
	std::uint64_t startBytes) noexcept
	: device_(&device), window_(std::move(window)), layout_(std::move(layout)),
	  startBytes_(startBytes) {}

RunScope RunScope::wholeSource(BlockDevice& source, std::vector<volume::Partition> partitions) {
	return RunScope{source, nullptr, std::move(partitions), 0};
}

RunScope RunScope::windowOnto(BlockDevice& source, const volume::Partition& partition) {
	auto window = std::make_unique<volume::PartitionView>(
		source,
		partition.startBytes,
		partition.lengthBytes);
	BlockDevice& windowed = *window;
	return RunScope{windowed, std::move(window), {}, partition.startBytes};
}

Result<RunScope> RunScope::resolve(BlockDevice& source, std::uint32_t partition) {
	const auto table = volume::readPartitionTable(source);
	if (partition == kWholeSource) {
		return wholeSource(source, layoutOf(table));
	}
	return table.andThen([&source, partition](const volume::PartitionTable& read) {
		return numbered(read, partition).map([&source](const volume::Partition& chosen) {
			return windowOnto(source, chosen);
		});
	});
}

BlockDevice& RunScope::device() noexcept {
	return *device_;
}

std::uint64_t RunScope::startBytes() const noexcept {
	return startBytes_;
}

std::span<const volume::Partition> RunScope::layout() const noexcept {
	return layout_;
}

} // namespace revenant::recovery

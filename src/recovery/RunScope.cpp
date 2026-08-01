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

// The entry an operator asked for, by the number the listing gave it.
[[nodiscard]] Result<volume::Partition>
entryNumbered(const volume::PartitionTable& table, std::uint32_t number) {
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
	std::vector<volume::Partition> layout) noexcept
	: device_(&device), window_(std::move(window)), layout_(std::move(layout)) {}

RunScope RunScope::windowOnto(BlockDevice& source, const volume::Partition& partition) {
	auto window = std::make_unique<volume::PartitionView>(
		source,
		partition.startBytes,
		partition.lengthBytes);
	BlockDevice& windowed = *window;
	return RunScope{windowed, std::move(window), {}};
}

Result<RunScope> RunScope::resolve(BlockDevice& source, std::uint32_t partition) {
	const auto table = volume::readPartitionTable(source);
	if (partition == kWholeSource) {
		return RunScope{source, nullptr, layoutOf(table)};
	}
	return table.andThen([&source, partition](const volume::PartitionTable& read) {
		return entryNumbered(read, partition).map([&source](const volume::Partition& chosen) {
			return windowOnto(source, chosen);
		});
	});
}

BlockDevice& RunScope::device() noexcept {
	return *device_;
}

std::span<const volume::Partition> RunScope::layout() const noexcept {
	return layout_;
}

} // namespace revenant::recovery

// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/PartitionListing.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/core/io/ImageFileDevice.hpp"
#include "revenant/volume/PartitionTable.hpp"

namespace revenant::cli {

namespace {

// The one line a source with no table earns. It is an answer, not a complaint:
// most images this tool is pointed at are a single volume.
constexpr std::string_view kNoTable =
	"partitions: none; the source is a single volume (or its table is unreadable)";

[[nodiscard]] std::string_view schemeName(volume::PartitionScheme scheme) {
	if (scheme == volume::PartitionScheme::kGpt) {
		return "GPT";
	}
	return "MBR";
}

// A GPT that answered from its backup copy is a damaged GPT, and the operator
// should know before they trust the rest of the disk.
[[nodiscard]] std::string_view backupNote(bool fromBackupHeader) {
	if (!fromBackupHeader) {
		return {};
	}
	return " (read from the backup header)";
}

[[nodiscard]] std::string headingFor(const volume::PartitionTable& table) {
	return "partitions: " + std::string{schemeName(table.scheme)} + ", " +
		   std::to_string(table.partitions.size()) + " found" +
		   std::string{backupNote(table.fromBackupHeader)};
}

// Offsets and lengths are printed as bytes rather than rounded: this output is
// as often piped into the next command as read, and a rounded offset is not one.
[[nodiscard]] std::string lineFor(const volume::Partition& partition) {
	return "  " + std::to_string(partition.number) + ": offset " +
		   std::to_string(partition.startBytes) + ", length " +
		   std::to_string(partition.lengthBytes) + ", " + partition.label;
}

[[nodiscard]] std::vector<std::string> linesFor(const volume::PartitionTable& table) {
	std::vector<std::string> lines{headingFor(table)};
	for (const volume::Partition& partition : table.partitions) {
		lines.push_back(lineFor(partition));
	}
	return lines;
}

[[nodiscard]] std::vector<std::string> describe(BlockDevice& device) {
	const auto table = volume::readPartitionTable(device);
	if (!table.hasValue() || table.value().partitions.empty()) {
		return {std::string{kNoTable}};
	}
	return linesFor(table.value());
}

} // namespace

Result<std::vector<std::string>> describePartitions(const std::filesystem::path& source) {
	auto device = ImageFileDevice::open(source);
	if (!device.hasValue()) {
		return device.error();
	}
	return describe(*device.value());
}

} // namespace revenant::cli

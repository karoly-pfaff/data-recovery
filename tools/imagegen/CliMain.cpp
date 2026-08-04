// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/CliMain.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "imagegen/CarveCorpus.hpp"
#include "imagegen/CliArgs.hpp"
#include "imagegen/PatternWriter.hpp"
#include "imagegen/SoakImage.hpp"
#include "imagegen/disk/DiskImageBuilder.hpp"
#include "imagegen/ntfs/NtfsImageBuilder.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/log/LogLevel.hpp"
#include "revenant/core/log/Logger.hpp"
#include "revenant/core/log/StderrSink.hpp"

namespace revenant::imagegen {

namespace {

// Defined below the verb table, which it is built from: a verb and the line
// documenting it are one fact, and this tool had them as two.
bool reportUsageError(Logger& logger);

// One writer failed; which one is the caller's to name, because "NTFS image"
// and "carve corpus" are the words an operator would use.
bool reportWriteError(Logger& logger, std::string_view what) {
	logger.log(LogLevel::kError, std::string{what} + " generation failed while writing");
	return false;
}

// Turns a writer's result into the exit this CLI reports, and says which writer
// failed. A request the writer cannot honour — a soak whose plants do not fit in
// the size asked for — is a usage error rather than a write failure, because no
// amount of free disk would change the answer.
bool reportOutcomeOf(Logger& logger, std::string_view what, const Result<std::uint64_t>& written) {
	if (written.hasValue()) {
		return true;
	}
	if (written.error().code == ErrorCode::kInvalidArgument) {
		return reportUsageError(logger);
	}
	return reportWriteError(logger, what);
}

bool runPattern(std::span<char* const> args, Logger& logger) {
	const auto request = parsePatternArgs(args);
	if (!request.hasValue()) {
		return reportUsageError(logger);
	}
	return reportOutcomeOf(
		logger,
		"image",
		writeImage(request.value().outputPath, request.value().sizeBytes, request.value().pattern));
}

bool runCarve(std::span<char* const> args, Logger& logger) {
	const auto size = parseSize(argAt(args, kSizeIndex));
	if (!size.hasValue()) {
		return reportUsageError(logger);
	}
	return reportOutcomeOf(
		logger,
		"carve corpus",
		writeCarveCorpus(std::filesystem::path{argAt(args, kOutputIndex)}, size.value()));
}

bool runSoak(std::span<char* const> args, Logger& logger) {
	const auto size = parseSize(argAt(args, kSizeIndex));
	const auto plants = parseSize(argAt(args, kPlantCountIndex));
	if (!size.hasValue() || !plants.hasValue()) {
		return reportUsageError(logger);
	}
	return reportOutcomeOf(
		logger,
		"soak image",
		writeSoakImage(
			std::filesystem::path{argAt(args, kOutputIndex)},
			size.value(),
			plants.value()));
}

bool writeNtfs(std::span<char* const> args, std::uint32_t records, Logger& logger) {
	return reportOutcomeOf(
		logger,
		"NTFS image",
		ntfs::writeNtfsImage(std::filesystem::path{argAt(args, kOutputIndex)}, records));
}

bool runNtfs(std::span<char* const> args, Logger& logger) {
	return writeNtfs(args, ntfs::kMftRecordCount, logger);
}

// The same volume with a bigger `$MFT`, which is what the `ntfs-enumerate`
// benchmark needs: seven files are enumerated faster than a process starts.
bool runNtfsWithRecords(std::span<char* const> args, Logger& logger) {
	const auto records = parseSize(argAt(args, kSizeIndex));
	if (!records.hasValue() || records.value() < ntfs::kMftRecordCount) {
		return reportUsageError(logger);
	}
	return writeNtfs(args, static_cast<std::uint32_t>(records.value()), logger);
}

bool runDisk(std::span<char* const> args, Logger& logger) {
	return reportOutcomeOf(
		logger,
		"disk image",
		disk::writeMbrDiskImage(std::filesystem::path{argAt(args, kOutputIndex)}));
}

// A verb is its name *and* its argument count together: a verb with the wrong
// number of arguments is a usage error, not a differently-shaped request. It
// also carries the operands it takes, so the usage text below is this table
// rather than a second copy of it.
struct Verb {
	std::string_view name;
	std::size_t argCount;
	bool (*run)(std::span<char* const>, Logger&);
	std::string_view operands;
};

constexpr std::array<Verb, 6> kVerbs{
	Verb{
		.name = "pattern",
		.argCount = kPatternArgs,
		.run = runPattern,
		.operands = "<output> <size-bytes> <zero|counter|lba>"},
	Verb{
		.name = "carve",
		.argCount = kSizedArgs,
		.run = runCarve,
		.operands = "<output> <size-bytes>"},
	Verb{
		.name = "soak",
		.argCount = kPlantedArgs,
		.run = runSoak,
		.operands = "<output> <size-bytes> <plant-count>"},
	Verb{.name = "ntfs", .argCount = kNamedArgs, .run = runNtfs, .operands = "<output>"},
	Verb{
		.name = "ntfs",
		.argCount = kSizedArgs,
		.run = runNtfsWithRecords,
		.operands = "<output> <mft-records>"},
	Verb{.name = "disk", .argCount = kNamedArgs, .run = runDisk, .operands = "<output>"}};

bool reportUsageError(Logger& logger) {
	logger.log(LogLevel::kError, usageText());
	return false;
}

bool dispatch(std::span<char* const> args, Logger& logger) {
	const auto verb = argAt(args, kVerbIndex);
	for (const Verb& candidate : kVerbs) {
		if (candidate.name == verb && candidate.argCount == args.size()) {
			return candidate.run(args, logger);
		}
	}
	return reportUsageError(logger);
}

} // namespace

std::string usageText() {
	std::string text{"usage:"};
	for (const Verb& verb : kVerbs) {
		text += "\n  revenant-imagegen ";
		text += verb.name;
		text += " ";
		text += verb.operands;
	}
	return text;
}

bool runCli(std::span<char* const> args) {
	StderrSink sink;
	Logger logger{sink, LogLevel::kInfo};
	if (args.size() <= kVerbIndex) {
		return reportUsageError(logger);
	}
	return dispatch(args, logger);
}

} // namespace revenant::imagegen

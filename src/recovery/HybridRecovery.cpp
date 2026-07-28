// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/HybridRecovery.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "revenant/carve/CandidateVisitor.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/ntfs/BootSector.hpp"
#include "revenant/fs/ntfs/EntryEnumeration.hpp"
#include "revenant/fs/ntfs/MftTable.hpp"
#include "revenant/recovery/ByteAccounting.hpp"

namespace revenant::recovery {

namespace {

constexpr std::size_t kBootSectorBytes = 512;

// Everything the filesystem pass finds passes through here on its way to the
// caller. The run needs to know what was accounted for and how much was
// reported, and neither can be asked afterwards without holding every entry in
// memory — which is exactly what a terabyte device forbids.
class AccountingVisitor final : public fs::EntryVisitor {
public:
	AccountingVisitor(fs::EntryVisitor& downstream, ByteAccounting& accounting) noexcept
		: downstream_(&downstream), accounting_(&accounting) {}

	void onEntry(const fs::RecoveredEntry& entry) override {
		accounting_->account(entry);
		downstream_->onEntry(entry);
		++reported_;
	}

	[[nodiscard]] std::uint64_t reported() const noexcept {
		return reported_;
	}

private:
	fs::EntryVisitor* downstream_;
	ByteAccounting* accounting_;
	std::uint64_t reported_ = 0;
};

[[nodiscard]] Result<fs::ntfs::NtfsGeometry> readGeometry(BlockDevice& device) {
	std::vector<std::byte> sector(kBootSectorBytes, std::byte{0});
	const auto read = device.readAt(0, sector);
	if (!read.hasValue()) {
		return read.error();
	}
	if (read.value() != sector.size()) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = 0};
	}
	return fs::ntfs::parseBootSector(sector);
}

// Mounting the volume and walking it. The vertical slice has one filesystem;
// the seam that makes this polymorphic arrives with the second one (M3).
[[nodiscard]] Result<fs::ntfs::EnumerationStats>
enumerateVolume(BlockDevice& device, fs::EntryVisitor& visitor) {
	return readGeometry(device)
		.andThen([&device](const fs::ntfs::NtfsGeometry& geometry) {
			return fs::ntfs::MftTable::open(device, geometry);
		})
		.andThen([&visitor](const fs::ntfs::MftTable& table) {
			return fs::ntfs::enumerateEntries(table, visitor);
		});
}

} // namespace

HybridRecovery::HybridRecovery(const carve::SignatureScanner& scanner, RecoveryMode mode) noexcept
	: scanner_(&scanner), mode_(mode) {}

Result<HybridRecovery::FilesystemPass> HybridRecovery::mountFailure(Error error) const {
	if (mode_ == RecoveryMode::kFilesystemOnly) {
		return error;
	}
	return FilesystemPass{.accounting = {}, .entries = 0, .mounted = false};
}

Result<HybridRecovery::FilesystemPass>
HybridRecovery::runFilesystemPass(BlockDevice& device, fs::EntryVisitor& visitor) const {
	if (mode_ == RecoveryMode::kCarveOnly) {
		return FilesystemPass{.accounting = {}, .entries = 0, .mounted = false};
	}
	ByteAccounting accounting;
	AccountingVisitor tee{visitor, accounting};
	const auto walked = enumerateVolume(device, tee);
	if (!walked.hasValue()) {
		return mountFailure(walked.error());
	}
	return FilesystemPass{
		.accounting = std::move(accounting),
		.entries = tee.reported(),
		.mounted = true};
}

std::vector<carve::ScanRegion>
HybridRecovery::carveRegions(const FilesystemPass& pass, std::uint64_t deviceSize) const {
	if (mode_ == RecoveryMode::kFilesystemOnly) {
		return {};
	}
	// An empty accounting yields one gap covering the device, which is exactly
	// what carve-only and an unmountable volume both want.
	return pass.accounting.gaps(deviceSize);
}

Result<HybridRecovery::ScanTotals> HybridRecovery::scanNextRegion(
	BlockDevice& device,
	carve::ScanRegion region,
	carve::CandidateVisitor& visitor,
	ScanTotals totals) const {
	const auto stats = scanner_->scanRegion(device, region, visitor);
	if (!stats.hasValue()) {
		return stats.error();
	}
	return ScanTotals{
		.candidates = totals.candidates + stats.value().candidateCount,
		.regions = totals.regions + 1};
}

Result<HybridRecovery::ScanTotals> HybridRecovery::scanRegions(
	BlockDevice& device,
	std::span<const carve::ScanRegion> regions,
	carve::CandidateVisitor& visitor) const {
	Result<ScanTotals> totals = ScanTotals{.candidates = 0, .regions = 0};
	for (const carve::ScanRegion& region : regions) {
		totals = scanNextRegion(device, region, visitor, totals.value());
		if (!totals.hasValue()) {
			break;
		}
	}
	return totals;
}

RecoveryStats HybridRecovery::statsOf(const FilesystemPass& pass, const ScanTotals& totals) {
	return RecoveryStats{
		.entriesReported = pass.entries,
		.candidatesReported = totals.candidates,
		.accountedBytes = pass.accounting.accountedBytes(),
		.regionsScanned = totals.regions,
		.regionsDropped = pass.accounting.droppedRegions(),
		.filesystemMounted = pass.mounted};
}

Result<RecoveryStats> HybridRecovery::run(
	BlockDevice& device,
	fs::EntryVisitor& entries,
	carve::CandidateVisitor& candidates) const {
	const auto pass = runFilesystemPass(device, entries);
	if (!pass.hasValue()) {
		return pass.error();
	}
	const auto regions = carveRegions(pass.value(), device.sizeInBytes());
	return scanRegions(device, regions, candidates).map([&pass](const ScanTotals& totals) {
		return statsOf(pass.value(), totals);
	});
}

} // namespace revenant::recovery

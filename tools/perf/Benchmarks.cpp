// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "perf/Benchmark.hpp"
#include "perf/BenchmarkInput.hpp"
#include "revenant/carve/BuiltinCarvers.hpp"
#include "revenant/carve/CandidateVisitor.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/ScanCandidate.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/fs/Mount.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "support/InMemoryDevice.hpp"

// The suite itself. Each body returns the work it did, so the harness divides
// rather than guessing; nothing here writes to a filesystem, so what is timed is
// the engine and not somebody's destination disk.

namespace revenant::perf {

namespace {

constexpr double kBytesPerMebibyte = 1024.0 * 1024.0;

// How many times the two *short* benchmarks repeat their work inside one timed
// repetition. A single MFT walk or carve attempt finishes in well under a
// millisecond, which is close enough to the clock's own noise that the spread
// swamped the median — measured at 1.2 and 0.75 before these existed. Doing
// more work per repetition is the honest fix: the work units scale with it, so
// the reported rate is unchanged and only its variance falls.
constexpr std::uint64_t kValidateRounds = 500;
constexpr std::uint64_t kEnumerateRounds = 2000;

// Counts what it is given. Every call here crosses a virtual dispatch into
// another translation unit, so there is nothing for the optimizer to elide.
class CountingCandidates final : public carve::CandidateVisitor {
public:
	void onCandidate(const carve::ScanCandidate& /*candidate*/) override {
		++seen_;
	}

	[[nodiscard]] std::uint64_t seen() const noexcept {
		return seen_;
	}

private:
	std::uint64_t seen_ = 0;
};

class CountingEntries final : public fs::EntryVisitor {
public:
	void onEntry(const fs::RecoveredEntry& /*entry*/) override {
		++seen_;
	}

	[[nodiscard]] std::uint64_t seen() const noexcept {
		return seen_;
	}

private:
	std::uint64_t seen_ = 0;
};

class SilentProgress final : public recovery::ScanProgress {
public:
	[[nodiscard]] bool onScanned(std::uint64_t /*bytes*/) override {
		return true;
	}
};

[[nodiscard]] carve::CarverRegistry everyCarver() {
	carve::CarverRegistry registry;
	carve::registerBuiltinCarvers(registry, {});
	return registry;
}

// MiB scanned per second, over an image with a known header density.
[[nodiscard]] std::uint64_t scanThroughput() {
	testing::InMemoryDevice device{scanImage(), 512};
	const auto registry = everyCarver();
	const carve::SignatureScanner scanner{registry, carve::ScanConfig{}};
	CountingCandidates candidates;
	const auto stats = scanner.scan(device, candidates);
	return static_cast<std::uint64_t>(
		static_cast<double>(stats.hasValue() ? stats.value().bytesScanned : 0) / kBytesPerMebibyte);
}

// Candidates validated per second: one accepted and one rejected per iteration,
// because rejecting cheaply matters as much as accepting.
[[nodiscard]] std::uint64_t carveOnce(const carve::CarverRegistry& registry) {
	std::uint64_t attempted = 0;
	for (const auto& carver : registry.carvers()) {
		ByteReader whole{validJpeg()};
		ByteReader cut{truncatedJpeg()};
		static_cast<void>(carver->carve(whole));
		static_cast<void>(carver->carve(cut));
		attempted += 2;
	}
	return attempted;
}

[[nodiscard]] std::uint64_t carveValidate() {
	const auto registry = everyCarver();
	std::uint64_t attempted = 0;
	for (std::uint64_t round = 0; round < kValidateRounds; ++round) {
		attempted += carveOnce(registry);
	}
	return attempted;
}

// Entries reported per second by the filesystem walk alone.
[[nodiscard]] std::uint64_t enumerateOnce(testing::InMemoryDevice& device, CountingEntries& into) {
	const auto mounted = fs::mountVolume(device);
	if (!mounted.hasValue()) {
		return 0;
	}
	static_cast<void>(mounted.value()->enumerate(into));
	return 1;
}

[[nodiscard]] std::uint64_t ntfsEnumerate() {
	testing::InMemoryDevice device{ntfsVolume(), 512};
	CountingEntries entries;
	for (std::uint64_t round = 0; round < kEnumerateRounds; ++round) {
		static_cast<void>(enumerateOnce(device, entries));
	}
	return entries.seen();
}

// The whole engine over the whole disk: every partition walked, then everything
// no volume accounted for scanned. Extraction is excluded on purpose — it
// measures the destination's disk, not this code.
[[nodiscard]] std::uint64_t endToEndHybrid() {
	testing::InMemoryDevice device{wholeDisk(), 512};
	const auto registry = everyCarver();
	const carve::SignatureScanner scanner{registry, carve::ScanConfig{}};
	const recovery::HybridRecovery hybrid{
		scanner,
		recovery::freshRun(recovery::RecoveryMode::kHybrid)};
	CountingEntries entries;
	CountingCandidates candidates;
	SilentProgress progress;
	static_cast<void>(hybrid.run(device, entries, candidates, progress));
	return static_cast<std::uint64_t>(
		static_cast<double>(device.sizeInBytes()) / kBytesPerMebibyte);
}

constexpr std::array<Benchmark, 4> kBenchmarks{{
	{.name = "scan-throughput", .unit = "MiB/s", .body = scanThroughput},
	{.name = "carve-validate", .unit = "candidates/s", .body = carveValidate},
	{.name = "ntfs-enumerate", .unit = "entries/s", .body = ntfsEnumerate},
	{.name = "end-to-end-hybrid", .unit = "MiB/s", .body = endToEndHybrid},
}};

} // namespace

std::span<const Benchmark> allBenchmarks() {
	return kBenchmarks;
}

} // namespace revenant::perf

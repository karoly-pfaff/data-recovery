// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

#include "cli/RecoveryRun.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/Sha256.hpp"
#include "revenant/recovery/CandidateIndex.hpp"
#include "revenant/recovery/HybridRecovery.hpp"

namespace revenant::cli {

// What a run is, reduced to one value: change the mode, the format allowlist or
// the device and the session in the destination belongs to a different recovery
// (ADR-0008), so resuming it would be wrong.
//
// `--dry-run` is deliberately not part of it. A preview and a real run scan the
// same bytes in the same order, so a preview can be promoted into an extraction
// without re-reading the device, which is exactly what ADR-0008 asks for.
[[nodiscard]] Sha256Digest shapeOf(const RunRequest& request, std::uint64_t sourceSize);

// The index a run appends to, and where its scan picks up.
struct OpenSession {
	recovery::CandidateIndex index;
	std::optional<std::uint64_t> resumeFrom;
};

// Continues the session already in the destination when it belongs to this same
// run, and starts a fresh one otherwise. A session that cannot be continued — no
// checkpoint, one belonging to another run, or one the index no longer agrees
// with — is not a broken run, it is just not a resumable one.
[[nodiscard]] Result<OpenSession> openSession(const RunRequest& request, std::uint64_t sourceSize);

// What a run reports its progress to: it writes a checkpoint, and answers
// whether to carry on — which is how Ctrl-C stops a scan cleanly instead of
// killing it.
class Checkpointer final : public recovery::ScanProgress {
public:
	Checkpointer(
		std::filesystem::path session,
		const Sha256Digest& shape,
		const recovery::CandidateIndex& index) noexcept;

	bool onScanned(std::uint64_t cursor) override;

	// Checkpoints that could not be written. A run that cannot say where it got
	// to still recovers what it finds — losing resumability is not worth losing
	// the data over — but it must not pretend it is resumable.
	[[nodiscard]] std::uint64_t unwritten() const noexcept;

private:
	std::filesystem::path session_;
	Sha256Digest shape_;
	const recovery::CandidateIndex* index_; // non-owning, never null
	std::uint64_t unwritten_ = 0;
};

} // namespace revenant::cli

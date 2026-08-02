// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/RunDamage.hpp"

#include <cstdint>
#include <utility>
#include <vector>

#include "core/SafeArith.hpp"
#include "recovery/Damage.hpp"
#include "revenant/core/io/SourceStack.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/recovery/ArtifactRecord.hpp"
#include "revenant/recovery/RecoverySink.hpp"

namespace revenant::cli {

recovery::Extraction marked(recovery::Extraction extraction, const DeliverySource& source) {
	const auto damage = source.stack->badRanges();
	if (damage.empty()) {
		return extraction;
	}
	for (recovery::ArtifactRecord& artifact : extraction.artifacts) {
		artifact.invented = recovery::inventedIn(artifact.extents, damage, source.startBytes);
		extraction.stats.degraded += artifact.invented.empty() ? 0U : 1U;
	}
	return extraction;
}

std::vector<recovery::ArtifactRecord>
onTheDevice(std::vector<recovery::ArtifactRecord> artifacts, std::uint64_t startBytes) {
	for (recovery::ArtifactRecord& artifact : artifacts) {
		for (fs::Extent& extent : artifact.extents) {
			extent.deviceOffset = saturatingAdd64(extent.deviceOffset, startBytes);
		}
	}
	return artifacts;
}

} // namespace revenant::cli

// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/RecoverySink.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "recovery/ExtractFile.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/recovery/Candidate.hpp"
#include "revenant/recovery/Disambiguate.hpp"
#include "revenant/recovery/OutputName.hpp"
#include "revenant/recovery/OutputPath.hpp"

namespace revenant::recovery {

namespace {

// Recovered data must not be written onto the media being recovered, so a
// destination that contains the source is refused outright (ADR-0005). Both
// sides are canonicalized first: two spellings of one directory are one
// directory.
[[nodiscard]] bool
contains(const std::filesystem::path& outer, const std::filesystem::path& inner) {
	std::error_code failed;
	const auto root = std::filesystem::weakly_canonical(outer, failed);
	const auto candidate = std::filesystem::weakly_canonical(inner, failed);
	const auto reach = std::ranges::mismatch(root, candidate);
	return reach.in1 == root.end();
}

[[nodiscard]] bool destinationIsUsable(const std::filesystem::path& destination) {
	std::error_code failed;
	return std::filesystem::is_directory(destination, failed);
}

} // namespace

RecoverySink::RecoverySink(std::filesystem::path destination)
	: destination_(std::move(destination)) {}

Result<RecoverySink>
RecoverySink::open(const std::filesystem::path& destination, const std::filesystem::path& source) {
	if (!destinationIsUsable(destination)) {
		return Error{.code = ErrorCode::kNotFound};
	}
	if (contains(destination, source)) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	return RecoverySink{destination};
}

std::optional<std::string> RecoverySink::claimName(const Candidate& winner, std::uint64_t ordinal) {
	const auto proposed = outputNameFor(winner, ordinal);
	if (!sanitizeOutputPath(destination_, proposed).hasValue()) {
		return std::nullopt;
	}
	auto claimed = disambiguate(proposed, [this](std::string_view name) {
		return used_.contains(std::string{name});
	});
	stats_.renamed += claimed == proposed ? 0U : 1U;
	used_.insert(claimed);
	return claimed;
}

Result<std::uint64_t>
RecoverySink::write(const Candidate& winner, BlockDevice& device, std::uint64_t ordinal) {
	const auto claimed = claimName(winner, ordinal);
	if (!claimed.has_value()) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	const auto target = sanitizeOutputPath(destination_, claimed.value());
	if (!target.hasValue()) {
		return target.error();
	}
	return extractTo(target.value(), winner, device);
}

void RecoverySink::record(const Result<std::uint64_t>& written) {
	if (!written.hasValue()) {
		++stats_.failed;
		return;
	}
	++stats_.filesWritten;
	stats_.bytesWritten += written.value();
}

ExtractionStats RecoverySink::extract(std::span<const Candidate> winners, BlockDevice& device) {
	std::uint64_t ordinal = 0;
	for (const Candidate& winner : winners) {
		record(write(winner, device, ordinal));
		++ordinal;
	}
	return stats_;
}

} // namespace revenant::recovery

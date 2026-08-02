// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/io/SourceStack.hpp"

#include <memory>
#include <span>
#include <utility>

#include "revenant/core/io/BadRange.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/core/io/RetryingDevice.hpp"

namespace revenant {

SourceStack::SourceStack(
	std::unique_ptr<BlockDevice> device,
	std::unique_ptr<RetryingDevice> retrying) noexcept
	: device_(std::move(device)), retrying_(std::move(retrying)) {}

SourceStack SourceStack::over(std::unique_ptr<BlockDevice> device, const RetryPolicy& policy) {
	auto retrying = std::make_unique<RetryingDevice>(*device, policy);
	return SourceStack{std::move(device), std::move(retrying)};
}

BlockDevice& SourceStack::top() noexcept {
	return *retrying_;
}

std::span<const BadRange> SourceStack::badRanges() const noexcept {
	return retrying_->badRanges();
}

} // namespace revenant

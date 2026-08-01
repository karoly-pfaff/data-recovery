// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/io/SourceStack.hpp"

#include <memory>
#include <span>
#include <utility>

#include "revenant/core/io/BadRange.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/core/io/CachingDevice.hpp"
#include "revenant/core/io/RetryingDevice.hpp"

namespace revenant {

SourceStack::SourceStack(
	std::unique_ptr<BlockDevice> device,
	std::unique_ptr<RetryingDevice> retrying,
	std::unique_ptr<CachingDevice> caching) noexcept
	: device_(std::move(device)), retrying_(std::move(retrying)), caching_(std::move(caching)) {}

SourceStack SourceStack::over(std::unique_ptr<BlockDevice> device) {
	// The policies are the documented defaults on purpose: what a run should
	// try before giving up on a sector is a property of failing hardware, not
	// of the source it was pointed at. An operator flag for it is a story for
	// whoever needs one.
	auto retrying = std::make_unique<RetryingDevice>(*device, RetryPolicy{});
	auto caching = std::make_unique<CachingDevice>(*retrying, CacheShape{});
	return SourceStack{std::move(device), std::move(retrying), std::move(caching)};
}

BlockDevice& SourceStack::top() noexcept {
	return *caching_;
}

std::span<const BadRange> SourceStack::badRanges() const noexcept {
	return retrying_->badRanges();
}

} // namespace revenant

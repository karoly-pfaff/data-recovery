// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. ADR-0005's destination rule, in two tiers (story-0609).
//
// The first is path spelling — the output tree must not grow around the source
// it reads — and it runs for every source, because it is the only tier that can
// answer for a path naming nothing, which is what an image that is not there
// looks like. Against a real device it never fires: a raw device path lies
// under no directory.
//
// The second is physical identity, and it runs only for a device source,
// because that is the case spelling cannot answer at all — `\\.\PhysicalDrive0`
// shares no path element with `C:\recovered`. A destination sharing a volume
// with a disk *image* is normal practice rather than a loss mode, so an image
// source never reaches it.

#include <filesystem>
#include <optional>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/DeviceIdentity.hpp"
#include "revenant/recovery/UnverifiedIdentity.hpp"

namespace revenant::recovery {

// The device tier's verdict over two identities, or nothing when the
// destination is somewhere the source does not reach.
//
// Two refusals, not one. A *proven* overlap is `kDestinationOnSource` and is
// absolute. An identity that could not be resolved is
// `kDestinationIdentityUnresolved`: the check cannot prove the destination is
// safe, so by default it does not gamble — but that is a question an operator
// can answer, and `kAllow` is them answering it. A VeraCrypt volume has no
// resolvable identity, and with one as the source this refused *every*
// destination, so the tool could not be pointed at an encrypted disk at all.
[[nodiscard]] std::optional<Error> refuseOverlap(
	const Result<StorageExtents>& source,
	const Result<StorageExtents>& destination,
	UnverifiedIdentity unverified = UnverifiedIdentity::kRefuse);

// The whole rule: the spelling tier, then the identity tier if the source is a
// device. Both judge the destination as the filesystem resolves it, so a
// junction cannot show one tier a different place than the other.
//
// `unverified` reaches the identity tier only. The spelling tier refuses a
// destination growing inside the source path whatever the operator says, and a
// proven overlap is refused the same way: neither is a question anyone was
// asked.
[[nodiscard]] std::optional<Error> destinationOnSource(
	const std::filesystem::path& destination,
	const std::filesystem::path& source,
	UnverifiedIdentity unverified = UnverifiedIdentity::kRefuse);

} // namespace revenant::recovery

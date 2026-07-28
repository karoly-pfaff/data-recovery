// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Reading one directory's slots off the device, and finding where its
// used slots end. Not a public interface.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fs/ClusterChain.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::fat {

// Every slot of the directory starting at `cluster`.
//
// A live directory is read through its FAT chain. A *deleted* one has no chain
// left — deletion frees it — and no length to guess from either, because a
// directory entry records size 0 for a directory. Only its first cluster can
// therefore be read, which is where the entries deleted alongside it are, and
// is the only extent that can be named without inventing one.
[[nodiscard]] Result<std::vector<std::byte>>
readDirectory(const ClusterChain& table, std::uint32_t cluster, bool freedChain);

// The slots up to the first one that has never been used. FAT's rule is that a
// zero first byte ends the directory: nothing after it was ever written, so
// nothing after it is worth reading.
[[nodiscard]] std::span<const std::byte> upToEndOfDirectory(std::span<const std::byte> bytes);

} // namespace revenant::fs::fat

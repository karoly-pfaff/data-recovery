// SPDX-License-Identifier: GPL-3.0-or-later
// Duplication-gate fixture: a run of declarations its neighbour repeats. See
// Computed.cc, which holds a function as well and is rejected all the same.

#include <cstdint>

namespace fixture::declared {

inline constexpr std::uint64_t kSignatureOffset = 0x00;
inline constexpr std::uint64_t kVersionOffset = 0x04;
inline constexpr std::uint64_t kBlockSizeOffset = 0x08;
inline constexpr std::uint64_t kBlockCountOffset = 0x10;
inline constexpr std::uint64_t kRootNodeOffset = 0x18;
inline constexpr std::uint64_t kFreeListOffset = 0x20;
inline constexpr std::uint64_t kChecksumOffset = 0x28;
inline constexpr std::uint64_t kFlagsOffset = 0x2C;
inline constexpr std::uint64_t kGenerationOffset = 0x30;
inline constexpr std::uint64_t kLabelOffset = 0x38;
inline constexpr std::uint64_t kCreatedOffset = 0x48;
inline constexpr std::uint64_t kModifiedOffset = 0x50;
inline constexpr std::uint64_t kNodeSizeOffset = 0x58;
inline constexpr std::uint64_t kNodeCountOffset = 0x5C;

} // namespace fixture::declared

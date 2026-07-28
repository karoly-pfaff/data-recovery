// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ntfs/EntryPath.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "revenant/fs/ntfs/MftRecord.hpp"
#include "revenant/fs/ntfs/MftTable.hpp"

namespace revenant::fs::ntfs {

namespace {

constexpr std::uint8_t kDosNameSpace = 2;

// One rung of the parent chain: the record a `$FILE_NAME` points at, and the
// sequence number that record must still carry for the link to be live. NTFS
// bumps the sequence when a slot is reused, which is how a stale reference is
// told from a live one.
struct ParentLink {
	std::uint64_t record;
	std::uint16_t sequence;
};

// A directory resolved from a link: its name, and its own link upwards.
struct PathStep {
	std::string segment;
	ParentLink parent;
};

// Where a climb stopped, and the directory names it collected — innermost
// first, the reverse of the order a path spells them.
struct Ascent {
	std::vector<std::string> directories;
	ParentLink stoppedAt;
};

[[nodiscard]] ParentLink linkTo(const MftFileName& name) noexcept {
	return ParentLink{.record = name.parentRecord, .sequence = name.parentSequence};
}

// A live link still points at the directory it named, rather than at whatever
// the slot was reused for since.
[[nodiscard]] bool isLiveDirectory(const MftRecordView& parent, const ParentLink& link) noexcept {
	return parent.isDirectory && parent.sequence == link.sequence;
}

[[nodiscard]] std::optional<PathStep> stepOf(const MftRecordView& parent) {
	const MftFileName* name = preferredName(parent.names);
	if (name == nullptr) {
		return std::nullopt;
	}
	return PathStep{.segment = name->name.utf8, .parent = linkTo(*name)};
}

// Nothing when the link is dead: the slot is unreadable, has been reused for
// another file, is not a directory, or carries no name to contribute.
[[nodiscard]] std::optional<PathStep> stepUp(const MftTable& table, const ParentLink& link) {
	const auto parsed = table.readRecord(link.record);
	if (!parsed.hasValue() || !isLiveDirectory(parsed.value(), link)) {
		return std::nullopt;
	}
	return stepOf(parsed.value());
}

// One rung: appends the directory the ascent currently points at and moves its
// link upwards. False when the walk is over — the root, or a broken chain.
[[nodiscard]] bool climb(const MftTable& table, Ascent& ascent) {
	if (ascent.stoppedAt.record == kRootRecordNumber) {
		return false;
	}
	std::optional<PathStep> step = stepUp(table, ascent.stoppedAt);
	if (!step.has_value()) {
		return false;
	}
	ascent.directories.push_back(std::move(step->segment));
	ascent.stoppedAt = step->parent;
	return true;
}

[[nodiscard]] Ascent ascend(const MftTable& table, ParentLink from) {
	Ascent ascent{.directories = {}, .stoppedAt = from};
	bool climbing = true;
	while (climbing && ascent.directories.size() < kMaxPathDepth) {
		climbing = climb(table, ascent);
	}
	return ascent;
}

[[nodiscard]] std::string
joinPath(const std::vector<std::string>& directories, const std::string& leaf) {
	std::string path;
	for (const std::string& directory : std::views::reverse(directories)) {
		path += directory;
		path += '/';
	}
	path += leaf;
	return path;
}

} // namespace

const MftFileName* preferredName(const std::vector<MftFileName>& names) {
	if (names.empty()) {
		return nullptr;
	}
	const auto longName = std::ranges::find_if(names, [](const MftFileName& name) {
		return name.nameSpace != kDosNameSpace;
	});
	return longName != names.end() ? &*longName : &names.front();
}

EntryPath resolveEntryPath(const MftTable& table, const MftRecordView& view) {
	const MftFileName* self = preferredName(view.names);
	if (self == nullptr) {
		return EntryPath{.path = {}, .reachedRoot = false};
	}
	const Ascent ascent = ascend(table, linkTo(*self));
	return EntryPath{
		.path = joinPath(ascent.directories, self->name.utf8),
		.reachedRoot = ascent.stoppedAt.record == kRootRecordNumber};
}

} // namespace revenant::fs::ntfs

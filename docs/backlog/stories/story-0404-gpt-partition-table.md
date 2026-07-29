<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0404: GPT partition table parser (+ protective MBR)

- Epic: [epic-m4-devices-partitions](../epic-m4-devices-partitions.md)
- Status: Done
- Size: M

## Goal

Read the table every disk larger than 2 TiB actually uses: the GUID Partition
Table. Two checksummed copies of a header, one at LBA 1 and one in the last
sector, each naming an array of 128-byte entries that is itself checksummed.
Falling back to the second copy when the first will not verify is not a nicety
here — it is the reason GPT keeps a second copy, and the situation a recovery
tool exists for.

## Design references

- [story-0403](story-0403-mbr-partition-table.md) — the scheme this sits beside,
  and the `SectorIo` bound (`byteOffsetOf`) both share.
- [ADR-0009](../../architecture/adr/adr-0009-output-safety.md) — bounded
  allocation. The entry array's size is stated by the very bytes it validates,
  so it is bounded before it is read.
- [ADR-0010](../../architecture/adr/adr-0010-filename-decoding-safe-output.md) —
  a GPT partition name is UTF-16LE, so `fs::decodeUtf16Name` decodes it and says
  whether it survived intact.

## Scope

1. **The header parser** (`include/revenant/volume/Gpt.hpp`) — `parseGptHeader`
   validates the header found at a stated LBA and returns `GptHeader`: where its
   twin is, the usable range, and where, how many and how large the entries are.
2. **The entry parser** — `parseGptEntry` reads one slot: its type GUID, its
   inclusive LBA range, and its name.
3. **The device read** (`include/revenant/volume/GptPartitions.hpp`) —
   `readGptPartitions` verifies the entry array against the header's CRC and
   yields a `GptPartition` per used entry, recording whether the answer came from
   the primary header or the backup.
4. **The protective MBR** — `volume::defersToGpt` answers the question that picks
   the scheme: does sector 0 say "the real table is the GPT"?
5. **A fuzz target** (`GptFuzz`) driving the whole read over an in-memory device,
   with a seeded corpus.

## Design decisions

**The CRC is checked before any other field is believed.** Every other value in
the header — where the entries are, how many there are, how big one is — is a
number this parser will act on, and the header's own CRC32 is the only evidence
that those numbers were written rather than landed there. So the order is:
enough bytes, the signature, a header size that fits, then the checksum, and only
then the fields. The CRC is computed with its own four bytes taken as zero, which
is how it was computed when it was written.

**A header must say where it is, and be right.** `MyLBA` is checked against the
LBA the sector was actually read from. A GPT header is copied verbatim to the
backup location except for this field and its twin, so believing a header at the
wrong place means believing the *other* copy's idea of where the entry array is
— which is a different array. This one field is what keeps the primary and the
backup from being interchangeable.

**Falling back to the backup covers the whole read, not just the header.** The
entry array has its own CRC, and an array that fails it is exactly as unusable as
a header that fails its own. So the unit that succeeds or fails is "a header and
the array it vouches for", and the backup is tried when any part of that fails.
A disk whose primary table was overwritten by a careless installer comes back
whole from the last sector, and the result says it did — silently substituting
the backup would hide that the disk is damaged.

**When both copies fail, the primary's rejection is the one reported.** It names
what the operator would look at first, and on a disk that never had a GPT it is
the honest answer ("no `EFI PART` at LBA 1") rather than a complaint about the
last sector.

**The entry array is bounded before it is read, not while.** `entryCount` and
`entrySize` are attacker-chosen 32-bit numbers whose product would size an
allocation. The product is checked against `kMaxEntryArrayBytes` first, and the
read is then bounded to the device by the same division `SectorIo` already uses.
An entry size below 128 or not a multiple of 8 is rejected outright: the spec
fixes both, and a size that breaks them cannot be walked.

**A name is trimmed at its first NUL, then decoded.** The 72-byte name field is
padded with zero code units, and handing the padding to `fs::decodeUtf16Name`
would come back as a name followed by thirty `%u0000` escapes — a lossless
encoding of nothing. The decoder's `lossless` flag then means what it should:
whether the *name* survived.

**An all-zero type GUID is the only "unused" there is.** GPT has no deleted
state and no tombstone: an entry either names a partition type or is sixteen
zero bytes. Nothing else about a zeroed entry is read, for the same reason an
unused MBR slot's bytes are not.

**The scheme question is "does this defer to a GPT", not "is this protective".**
The obvious predicate — *exactly* one used entry, of type `0xEE` — is true of a
pure GPT disk and false of a *hybrid* MBR, which carries the guard entry
alongside real ones so a legacy reader can still boot. But a hybrid's entries are
a curated subset of what its GPT holds, so reading them instead of the GPT hands
back a partial answer; and `readMbrPartitions` refuses any table with a `0xEE`
entry anyway, so the strict predicate would leave a readable disk with no
readable table at all. `defersToGpt` is therefore true whenever the guard entry
is present, which is exactly the condition `readMbrPartitions` refuses on — one
rule, asked from both sides.

## Acceptance criteria

- [x] `parseGptHeader` returns `GptHeader{myLba, alternateLba, firstUsableLba,
      lastUsableLba, entryArrayLba, entryCount, entryBytes, entryArrayCrc}`.
- [x] Input shorter than 92 bytes is `kOutOfRange` at the input's size.
- [x] A signature other than `EFI PART` is `kInvalidArgument` at `0x00`.
- [x] A header size below 92 or larger than the input is `kInvalidArgument` at
      `0x0C`; a header whose CRC32 does not match is `kInvalidArgument` at `0x10`.
- [x] A `MyLBA` other than the LBA the header was read from is
      `kInvalidArgument` at `0x18`.
- [x] A first usable LBA above the last usable LBA is `kInvalidArgument` at
      `0x28`.
- [x] An entry size below 128 or not a multiple of 8 is `kInvalidArgument` at
      `0x54`; an entry array larger than `kMaxEntryArrayBytes` is `kOutOfRange`
      at `0x50`.
- [x] `parseGptEntry` returns `GptEntry{typeGuid, firstLba, lastLba, name,
      nameIsExact}`; input shorter than 128 bytes is `kOutOfRange`, and a last
      LBA below the first is `kInvalidArgument` at `0x28`.
- [x] A name is decoded from the code units before its first NUL, and an entry
      whose name fills all 36 units decodes whole.
- [x] `isUnusedEntry` is true exactly when the type GUID is sixteen zero bytes.
- [x] `readGptPartitions` yields one `GptPartition{startBytes, lengthBytes,
      typeGuid, name, nameIsExact}` per used entry, in array order, with
      `fromBackupHeader == false`.
- [x] An entry array whose CRC32 does not match the header's is
      `kInvalidArgument` at `0x58`.
- [x] A disk whose primary header or array fails falls back to the header in its
      last sector and reports `fromBackupHeader == true`.
- [x] When both copies fail, the primary's error is returned.
- [x] `defersToGpt` is true for a protective table and for a hybrid one, and
      false for an empty table and a normal one.
- [x] `GptFuzz` exists with a seeded corpus and drives `readGptPartitions` over
      an in-memory device.

## Test plan

Unit (`tests/unit/volume/GptTest.cpp`): a known-good header parses to known
fields; one case per rejection above, asserting code *and* offset through
`testing::Rejection`; a header whose CRC was computed over the wrong bytes; a
header copied to the wrong LBA. Entries: a live entry with a name, an entry whose
name uses all 36 code units, an unused entry, a reversed LBA range, a truncated
slot.

Unit (`tests/unit/volume/GptPartitionsTest.cpp`): a two-partition disk yields two
partitions with byte offsets scaled by the device's sector size and lengths that
count the inclusive last LBA; a 4096-byte-sector disk scales by 4096; a disk
whose primary header is zeroed comes back from the backup with the flag set; a
disk whose primary *array* CRC is wrong does the same; a disk with neither
reports the primary's rejection; an entry array pointing past the end of the
device is rejected; a device too small to hold a backup header is rejected.

Unit (`tests/unit/volume/MbrTest.cpp`): `defersToGpt` over an empty table, a
protective one, a normal one, and a hybrid one.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.

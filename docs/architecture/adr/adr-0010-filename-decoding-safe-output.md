<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# ADR-0010: Filename decoding & cross-platform-safe output

- **Status:** Accepted
- **Date:** 2026-07-24

## Context

Each filesystem stores names differently: NTFS and exFAT use UTF-16, FAT short names use
an OEM code page (with a UTF-16 long-name extension), and ext4 stores raw bytes with no
enforced encoding. Recovered names must be decoded correctly and then written to a
destination whose filesystem has its own rules — and the source name may be partially
corrupt. Getting this wrong produces mojibake, lost names, or write failures, and it
interacts with the [output-safety](adr-0009-output-safety.md) rules.

## Decision

Filename handling is a distinct, tested step with an explicit pipeline:

1. **Decode** the on-disk name using the filesystem's known encoding into an internal
   canonical form (UTF-8), with a defined policy for undecodable/partial bytes (lossless
   escaping, never silent truncation).
2. **Preserve the original** decoded name (and its raw bytes) in the session manifest, so
   no information is lost even when the written filename must differ.
3. **Sanitize for the destination** via `sanitizeOutputPath`
   ([ADR-0009](adr-0009-output-safety.md)): apply target-OS validity and reserved-name
   rules, and confine to the destination root.
4. **Disambiguate** collisions deterministically (suffixing), so distinct recovered files
   never overwrite each other.

Decoding lives in the filesystem layer (it knows the encoding); sanitization lives in the
recovery/sink layer (it knows the destination). The two are separate responsibilities.

## Consequences

- Names survive round-trips across platforms (a UTF-16 NTFS name recovers correctly on a
  Linux ext4 destination and vice versa).
- Corrupt or undecodable name bytes degrade gracefully to a safe, recorded form rather
  than failing the whole entry.
- The manifest is the authoritative record of original names; the on-disk output name is a
  sanitized, possibly-renamed representation.
- Carved files (which have no name) are unaffected — they use `f<NNNNNNN>.<ext>` and rely
  on arbitration/provenance, not decoded names.

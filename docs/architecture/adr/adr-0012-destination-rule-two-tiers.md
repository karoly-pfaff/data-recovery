<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# ADR-0012: The destination rule is two tiers over physical identity

- **Status:** Accepted
- **Date:** 2026-08-06
- **Supersedes:** the *Validated* half of
  [ADR-0011](adr-0011-two-halves-of-the-read-only-guarantee.md)
- **Implements:** [ADR-0005](adr-0005-read-only-by-default.md)'s destination requirement

## Context

[ADR-0005](adr-0005-read-only-by-default.md) requires recovered output to land on storage
the source does not occupy, and says the CLI validates this before starting. It does not
say how, and the how turned out to matter twice.

The original check compared path spellings, written when every source was an image file.
A raw device shares no path element with anything, so a run pointed at
`\\.\PhysicalDrive0` with its output on a volume of that disk passed validation and wrote
onto the clusters it was reading. story-0609 fixed that.

[ADR-0011](adr-0011-two-halves-of-the-read-only-guarantee.md) recorded the state of the
guarantee on 2026-07-31, before that fix landed. Its *Validated* half describes the rule as
"a lexical path-prefix comparison in `RecoverySink`" which "does not hold for raw-device
sources", and names story-0609 as work still to come. All three statements were true when
written; none is true now. This record replaces that half.

The decision that replaced it was written into ADR-0005's Consequences **in place**, which
[ADR-0001](adr-0001-record-architecture-decisions.md) forbids: an Accepted ADR is
superseded by a new record, not edited. So the rule in force had no record of its own, and
the rule it replaced had no trace of being replaced. This ADR is that record, and
ADR-0005's Consequences are restored to the text that was accepted.

## Decision

The destination rule is **two tiers**, both judging the destination as the filesystem
resolves it, so a junction or symlink cannot show one tier a different place than the
other. It lives in `recovery::destinationOnSource`, not in `RecoverySink`.

**Tier one — path spelling. Every source.** The output tree must not grow inside the
source it reads. Against a real device it never fires: a raw device path lies under no
directory.

**Tier two — physical identity. Device sources only.** This is the case spelling cannot
answer at all. The seam is `StorageExtents` from `revenant/core/io/DeviceIdentity.hpp`:
`storageOf` for the source device, `storageUnder` for the destination directory, compared
by `overlaps`. A whole disk is expressed as one extent covering every byte (`kWholeDisk`),
so it rules out every volume on it; a volume rules out itself but allows a sibling volume
of the same disk, because the loss mode is overwriting the clusters under recovery and a
sibling holds none of them.

An **image** source never reaches tier two. A destination sharing a volume with a disk
image is normal practice, not a loss mode.

Which tier a source reaches follows from what the path *is* on the filesystem.
`classifySource` calls a directory `kNotBlockAddressable`, a regular file `kImageFile`, and
everything else `kDevice`, so it never learns how a device path is spelled — and **a path
naming nothing at all classifies as a device**. A missing image reaches tier two, where
`storageOf` cannot resolve it and the run is refused.

**An identity that cannot be resolved refuses the run.** When the check cannot prove the
destination is safe, it does not gamble — reading an unanswerable question as "elsewhere"
is how output lands on the source. An **empty** extent list is not an unresolved identity:
a network share or a tmpfs sits on no local disk, and that is a real answer, permitted on
the strength of holding no local storage.

## Consequences

- ADR-0005's requirement is enforced by mechanism for both source kinds, not by spelling
  alone. The *Validated* half of ADR-0011 is superseded: this is a real check, and a
  document restating it may now say so.
- **The containers the rule does not see through**, all cases where the OS answers about
  the container rather than about what carries it:
  - a Windows volume inside a **Storage Space** reports the *virtual* disk's extents;
  - a **mounted VHD** likewise;
  - on Linux a **loop-mounted image** is reported as a disk of its own rather than as the
    file it is, so a destination inside an image living on the source disk is not caught.

  None is a silent wrong answer about ordinary storage. This list is the authoritative one
  and belongs on 1.0's limits page.
- **Refusing on an unresolvable identity has a cost, and it is real.** A VeraCrypt volume
  has no identity Windows will resolve, so every destination is refused and the tool cannot
  run against one at all. Whether an operator may override that is **not decided here**;
  story-0707 is where it is being decided, and if it lands, the constraint on it is that it
  may relax only the unresolvable case and never the proven-overlap one. Until then this
  record describes a rule with no override, because that is the rule that exists.
- The rule is enforced before the first read, so a run that would violate it costs nothing
  but the argument check.
- Adding a source kind means deciding which tiers it reaches. That decision belongs in a
  successor to this record, not in a parameter.

## Alternatives considered

- **Keep the path comparison and document its limit.** Rejected: the limit is precisely the
  case with the worst outcome, and a documented way to destroy the source is not a
  guarantee.
- **Compare device identity only.** Rejected: it cannot answer for an image path that names
  nothing, and it would refuse the ordinary image-beside-its-output workflow.
- **Ask the OS whether two paths are on the same volume.** Rejected: that answers about
  filesystems, not storage. story-0609's design prescribed exactly such a call, nobody
  checked it against a real system, and the mistake was found only by adversarial audit
  after implementation — which is why the comparison is over storage extents.

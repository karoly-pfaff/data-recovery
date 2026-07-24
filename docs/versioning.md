<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Versioning & Release

## Semantic Versioning

Revenant follows [SemVer 2.0.0](https://semver.org): `MAJOR.MINOR.PATCH`.

- **MAJOR** — incompatible changes to a stable public surface (CLI flags/behaviour,
  on-disk output layout, or the `librevenant` API once it is declared stable).
- **MINOR** — new, backward-compatible capability (a new format carver, a new
  filesystem, a new flag).
- **PATCH** — backward-compatible bug fixes.

Pre-1.0 (`0.y.z`): the surface is unstable; `MINOR` may carry breaking changes. Each
milestone tags a `0.MINOR.0` pre-release (M1 → `v0.1.0`, and so on). `1.0.0` ships at the
end of M5.

The recovery accuracy of a given format/filesystem is part of the contract: a change
that makes recovery *worse* for a supported target is treated as breaking.

## Conventional Commits

Commit messages follow [Conventional Commits](https://www.conventionalcommits.org):

```
<type>(<optional scope>): <summary>

<optional body>

<optional footer, e.g. BREAKING CHANGE: ...>
```

Types: `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`, `ci`, `chore`.
Scopes match the module map, e.g. `feat(carve): add PNG validating carver`. A
`BREAKING CHANGE:` footer (or `!` after the type) drives the MAJOR/MINOR decision.

Commits should be small and buildable; each references its story where applicable.

## Changelog

`CHANGELOG.md` follows [Keep a Changelog](https://keepachangelog.com). Every
user-facing change adds an entry under `[Unreleased]` in the appropriate group (Added,
Changed, Deprecated, Removed, Fixed, Security). At release, `[Unreleased]` is renamed to
the new version with its date and a fresh `[Unreleased]` is opened.

## Release procedure

1. Ensure all quality gates are green on `main`.
2. Finalize `CHANGELOG.md`: move `[Unreleased]` to `vX.Y.Z` with the date.
3. Bump the version in `CMakeLists.txt` (`project(... VERSION X.Y.Z)`) and `vcpkg.json`.
4. Tag `vX.Y.Z` and let CI build and attach platform artifacts (from M5 onward).
5. Open a new `[Unreleased]` section.

## Compatibility promises

- Before `1.0.0`, nothing is promised stable except the read-only-source guarantee,
  which holds from day one and forever.
- From `1.0.0`, CLI behaviour and output layout are covered by SemVer; the
  `librevenant` API is covered only once explicitly declared stable in its own ADR.

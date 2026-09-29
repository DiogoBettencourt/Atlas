# Versioning

Atlas follows [Semantic Versioning](https://semver.org)
(`MAJOR.MINOR.PATCH`), with the specifics below made explicit since
"what counts as what" is otherwise a judgment call every time.

## What each number means here

- **PATCH** (`0.5.0` → `0.5.1`) - a fix, with no new user-facing
  capability and no behavior change beyond "the thing that was broken
  now works as originally intended." Example: v0.5.1 fixed the published
  npm package silently shipping without its `atlas` command - nothing
  new was added, a bug in an already-shipped feature was closed.
- **MINOR** (`0.5.1` → `0.6.0`) - a new, backward-compatible capability.
  Existing usage keeps working unchanged; something new is now possible.
  Example: v0.6.0 added the full-screen TUI and server auto-start -
  additive, nothing that worked before stopped working.
- **MAJOR** (`0.x.y` → `1.0.0`, and beyond) - reserved for a breaking
  change, or for the 1.0 milestone itself (see `ROADMAP.md`'s "What
  1.0 would mean" section) - the point at which Atlas commits to not
  breaking the REST API contract casually. Not used yet; every release
  so far has been `0.x`.

**Pre-1.0 note:** SemVer technically allows anything to change at any
time before `1.0.0`. Atlas holds itself to the stricter practice above
anyway (patch = fix only, minor = additive only) because the release
history already reads that way, and a version number that means
something consistently is more useful than the SemVer spec's minimum
bar - "it's 0.x, all bets are off" is a bad reason to make an
inconsistent release stream harder to reason about.

## What actually gets versioned

Right now there's a mismatch worth knowing about rather than discovering
by surprise:

- **GitHub releases** (`v0.3.0` … `v0.6.0` today) version the project /
  monorepo as a whole - a release can (and so far always has) bundled
  changes to the backend, AtlasCLI, or both.
- **`CMakeLists.txt`**'s `project(Atlas VERSION ...)` is still `0.1.0` -
  it has not been bumped alongside any GitHub release.
- **`packages/cli/package.json`**'s `"version"` is also still `0.1.0` -
  same gap; the npm-published package version has never moved past its
  initial publish.
- Neither the `atlas` binary nor the `atlas` CLI command currently expose
  a `--version` flag at all, so there's no way to ask either one at
  runtime what it actually is.

None of this blocks a release today (GitHub's tag is the source of truth
in practice), but it means "check the version" doesn't currently work the
way someone would expect from either artifact. Worth a follow-up to: bump
both files to track the GitHub release version going forward (or decide
they should version independently and say so explicitly), and add a real
`--version` flag to both the backend and the CLI.

## Practical flow for cutting a release

1. Land the PR(s) for the release into `dev`.
2. Fast-forward `master` to `dev`.
3. Tag and publish a GitHub release against `master`, named `vX.Y.Z`,
   with release notes in the existing format: one-line summary, an
   `## Added`/`## Fixed` section per change linking its PR, a carried-
   forward `## Known limitations` section, and a `Full diff` compare
   link to the previous tag.
4. If the change touched AtlasCLI in a way users installing from npm
   would want, separately bump `packages/cli/package.json` and publish
   to npm - this is not currently automatic or guaranteed to happen
   every GitHub release (see v0.6.0, which shipped a GitHub release
   without a matching npm publish).

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

The whole project shares **one version**, and it is the GitHub release tag
(`vX.Y.Z`). That number lives in exactly two files, and they are bumped
together:

- **`CMakeLists.txt`**'s `project(Atlas VERSION X.Y.Z ...)` - the backend.
  It is compiled into the binary, so `atlas --version` prints it and the
  startup banner shows it. There is no second hand-maintained string.
- **`packages/cli/package.json`**'s `"version"` (and the matching two
  entries at the top of `package-lock.json`) - AtlasCLI. `atlas --version`
  on the CLI reads it at runtime.

A test in `packages/cli/src/version.test.ts` fails CI if the two ever
disagree, so skew can't ship by accident.

What is *not* automatic: publishing to npm. A GitHub release always
carries the new version in both files, but the npm package is only
re-published when someone decides AtlasCLI changes are worth shipping
there (v0.6.0 through v0.8.0 were not). When it is published, it goes out
under the same number as the release it came from, never an independent
one.

## Practical flow for cutting a release

1. Land the PR(s) for the release into `dev`.
2. Bump the version in `CMakeLists.txt` and `packages/cli/package.json`
   (+ `package-lock.json`) to `X.Y.Z` in a final commit on `dev`, then
   fast-forward `master` to `dev`.
3. Tag and publish a GitHub release against `master`, named `vX.Y.Z`,
   with release notes in the existing format: one-line summary, an
   `## Added`/`## Fixed` section per change linking its PR, a carried-
   forward `## Known limitations` section, and a `Full diff` compare
   link to the previous tag.
4. If the change touched AtlasCLI in a way users installing from npm
   would want, publish `packages/cli` to npm (its version was already
   bumped in step 2) - this is not automatic and doesn't have to happen
   for every GitHub release.

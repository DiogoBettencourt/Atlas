# Operating self-improvement mode safely

Status: guidance / spec for the human on the other end of `github_pr`,
complementing the "Safety properties, by construction" section of the
README (which covers what the C++ code itself enforces). This doc covers
the parts that are necessarily *your* responsibility, not the code's -
Atlas opens pull requests; it never merges one.

## Repo layout: use a dedicated clone, not your daily working copy

`--self-repo` should point at a clone (or `git worktree`) you don't
otherwise use for interactive development. `GitTool` operates directly
against whatever is checked out at that path: `checkout_branch` switches
the branch of that exact working tree, and `add`/`commit` will pick up
*any* dirty files sitting there, not just ones the agent itself wrote in
this session. If `--self-repo` is your normal dev checkout and you've got
uncommitted work in progress there, an unrelated self-improvement PR can
end up carrying your WIP changes along with it, or a checkout_branch call
mid-edit can switch your own working tree out from under you.

A cheap way to get a dedicated one without a second full clone:

```bash
git worktree add /path/to/atlas-self-improve testing_atlas_rework
```

then point `--self-repo` at `/path/to/atlas-self-improve` and leave it
alone otherwise.

## GitHub token scope

Use a fine-grained PAT, scoped to this one repository only, with exactly:

- **Contents: Read and write** (needed for `git push`)
- **Pull requests: Read and write** (needed for `github_pr`)

Nothing else. Set an expiration on it - self-improvement mode is
something you turn on for a session, not a standing credential. Only
export `ATLAS_GITHUB_TOKEN` in the shell you're about to run that session
in; don't put it in a shell profile that's sourced for everyday work.

## Branch protection on GitHub

`github_pr` only ever calls `POST /repos/{repo}/pulls` - it cannot merge,
approve, or force-push (see the README's safety-properties list). But
that guarantee is only worth as much as your GitHub branch protection
rules make it: turn on "Require a pull request before merging" and
"Require status checks to pass" (once CI is set up - see
`.github/workflows/ci.yml`) on whichever branch `github_pr`'s `base`
targets, so there's no path from "Atlas opened a PR" to "code is live"
that skips your review, even by accident (a second collaborator's token,
a misconfigured automation, etc).

## What to look for when reviewing one of these PRs specifically

Beyond ordinary code review:

- **Read the diff for changes to the safety-relevant files themselves**:
  `WorkspaceManager.cpp` (`resolveSafe`), `GitTool.cpp`, `GitHubPRTool.cpp`.
  A PR that touches its own sandboxing logic deserves more scrutiny than
  one that doesn't, precisely because those are the files standing
  between "an agent with an LLM's judgment" and "arbitrary file/git
  access." This is explicitly called out as unsolved in the README's
  "What this doesn't give you" section - nothing currently flags this
  automatically.
- **Check what workspace the request was made against.** `steps` in the
  `/chat` response (or the NDJSON stream) shows every tool call the agent
  made - skim it for anything unexpected before trusting the diff matches
  the PR description.
- **Run the smoke tests locally** (`tests_manual/*.cpp`, or `ctest` once
  CI is wired up) rather than only reading the diff - several of the bugs
  this project's own PRs have fixed (a sandbox-escape edge case, a
  cross-workspace data leak) were the kind that look fine on read-through
  and only show up when actually exercised.

## Known gap: unbounded session growth

`Agent::chat` resends the entire session history on every iteration
(`session_manager_.getHistory(session_id)`), and there's currently no
summarization, truncation, or session-reset mechanism. A long-running
self-improvement session (many back-and-forth iterations against a large
repo) will keep growing its context on every turn with no ceiling besides
the model's own context window and `max_iterations_`. If a self-improvement
session starts behaving oddly after a long conversation, starting a fresh
`session_id` is the immediate workaround; a real fix (windowing, summary
compaction) is out of scope for this doc but worth tracking as a follow-up.

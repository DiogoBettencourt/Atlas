# Roadmap

Status: living doc, edited as goals firm up or shift - not a fixed contract.

## Goals

- **Local-first, privacy-focused.** No cloud dependency for the core loop
  and no telemetry - a user's workspaces, sessions, and history live on
  their own disk as plain JSON (see the README's "Local Data
  Persistence" framing), and Atlas talks to a model running on their own
  machine (Ollama) rather than a hosted API by default.
- **A real agent runtime, not just a chatbot.** The point of the
  workspace/tool/session model is to support actual autonomous work
  against a codebase (read/search/edit/git/PR), with the self-improvement
  loop (`--self-repo`) as the proving ground - Atlas opening real,
  human-reviewed PRs against its own repo is meant to be a genuine
  capability, not a demo.
- **Hardware fairness, AMD included.** Atlas is deliberately not assuming
  an NVIDIA/CUDA-only local-AI stack. `docs/specs/local-gpu-inference.md`
  exists because AMD/ROCm has real, currently-undocumented-elsewhere rough
  edges (see #32) - closing that gap is a stated goal, not an
  afterthought.
- **More than one client, one backend.** Atlas itself stays a headless
  REST service; AtlasCLI is the first client, AtlasUI (graphical) is
  planned. Any client should be able to talk to the same API.
- **Minimal, well-understood dependencies.** `FetchContent`-fetched
  nlohmann/json, standalone Asio, cpp-httplib on the backend; no database,
  no framework the size of the actual application logic.

## Milestones

Roughly chronological; "done" milestones are here for context, not as
busywork to re-verify.

### Done

- **Backend core** - `Agent`'s ReAct loop, the seven built-in tools,
  workspace sandboxing (`WorkspaceManager::resolveSafe`), per-session JSON
  persistence, bounded history with chunked compaction (#17, fixed).
- **Self-improvement loop** - `git`/`github_pr` tools, hard-blocked at the
  workspace and branch level, used for real to ship several of the
  releases below.
- **AtlasCLI v1** - config file, session picker, `/chat/stream` with
  reconnect + non-streaming fallback, npm distribution
  (`@diogobettencourt/atlas-cli`).
- **AtlasCLI v2 (v0.6.0)** - full-screen TUI, Chat/Sessions tab bar,
  server auto-start (`--atlas-binary`).

### Near-term - CLI reliability

The full-screen rework shipped ahead of these being solid; closing them
out is the immediate priority before building more CLI surface on top:

- #28 - `/exit`/`/quit` don't actually terminate the process.
- #29 - switching sessions doesn't load or preserve that session's chat
  history (no fetch-history client call exists yet either).
- #22 - no end-to-end CI running the real `atlas` binary against a stub
  server.

### Mid-term - model management

- #30 - model hierarchy/routing: stop sending every turn (trivial chat
  and real coding work alike) to the same one configured model.
- #31 - hardware-aware model setup: detect GPU/VRAM, recommend or pull
  models that will actually run well, instead of finding out the hard
  way.

### Ongoing, cross-cutting

- #32 - AMD-specific support and optimization. Not tied to a single
  version bump - a continuous investment area (docs, testing across
  generations, tracking what's verified) rather than a checkbox.

### Not started

- **AtlasUI v1** (#55) - React web client, chat + sessions with CLI parity.
  Designed and built; see `packages/ui`. Still to come after v1: a Stop
  button (needs cancellation in the backend), token-level streaming, a
  workspace picker, a desktop wrapper.
- **Standalone binary distribution** for AtlasCLI (`pkg`/`nexe`/Node's SEA)
  so a no-Node-required install exists - currently npm-only.

### What "1.0" would mean

Not committed to yet, but the rough bar before calling this stable rather
than "actively developed, breaking changes possible":

- The REST API contract (`/health`, `/chat`, `/chat/stream`) is one Atlas
  is willing to commit to *not* breaking casually.
- The CLI reliability issues above (#28, #29, #22) are closed - a 1.0
  client shouldn't have a broken `/exit`.
- The model-setup story (#31) means a new user isn't guessing whether
  their hardware can even run this well.
- No known safety gap in the self-improvement tooling beyond what
  `docs/specs/self-improvement-safety.md` already documents and accepts.

This list itself should be revisited as the project's actual priorities
become clearer - it's a first pass, not a locked scope.

# Atlas

Local-first, privacy-focused C++20 AI workspace backend. Headless REST
microservice that drives a local Ollama model through a ReAct (Reason +
Act) tool-calling loop, with sandboxed filesystem tools and a lightweight
codebase symbol indexer ("Librarian" architecture) so agents don't need to
dump whole repositories into context.

## Requirements

- CMake >= 3.20
- A C++20 compiler (GCC 11+, Clang 14+, MSVC 2019+)
- OpenSSL development headers — optional, only needed for the `github_pr`
  tool's HTTPS calls to `api.github.com`. If CMake can't find OpenSSL,
  Atlas still builds and runs; `github_pr` just self-disables with a clear
  error at runtime. On Windows, the easiest path is
  `vcpkg install openssl:x64-windows` and pointing CMake at vcpkg's
  toolchain file; on Debian/Ubuntu, `apt install libssl-dev`.
- [Ollama](https://ollama.com) running locally, with a model pulled, e.g.:

  ```bash
  ollama pull qwen2.5-coder:14b
  ollama serve
  ```

Everything else (nlohmann/json, standalone Asio, cpp-httplib) is fetched
automatically by CMake's `FetchContent` on first configure — no manual
dependency install needed, but the machine building Atlas does need
network access to GitHub.

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

This produces the `atlas` executable in `build/`.

## Run

```bash
./atlas \
  --model=qwen2.5-coder:14b \
  --port=8080 \
  --bind=127.0.0.1 \
  --data-dir=./atlas_data/storage \
  --workspaces-dir=./atlas_data/workspaces \
  --ollama-host=127.0.0.1 \
  --ollama-port=11434
```

All flags are optional and default to the values shown above. On startup
Atlas creates a `default` workspace under `--workspaces-dir` and indexes it
for `search_symbol`.

## API

### `GET /health`
Liveness check. Returns `{"status": "ok"}`.

### `POST /chat`
```json
{
  "session_id": "any-string-you-choose",
  "message": "What does WorkspaceManager::resolveSafe do?",
  "workspace": "default"
}
```
`workspace` is optional (defaults to `"default"`) and names a sandbox
directory under `--workspaces-dir`; it's created automatically if it
doesn't exist yet.

Response:
```json
{
  "session_id": "any-string-you-choose",
  "reply": "..."
}
```

Chat history is persisted per `session_id` as JSON under
`<data-dir>/sessions/<session_id>.json`, so conversations survive restarts.

## Built-in tools

The agent has six tools available on every turn:

| Tool            | Purpose                                                        |
|-----------------|------------------------------------------------------------------|
| `search_symbol` | Find classes/functions/methods by name across the indexed workspace |
| `read_file`     | Read a workspace-relative file (truncated past 32KB)             |
| `write_file`    | Create or fully overwrite a workspace-relative file               |
| `edit_file`     | Exact, unique find-and-replace edit within an existing file       |
| `git`           | Restricted git ops (`status`/`diff`/`add`/`commit`/`checkout_branch`/`push`) — self-repo only |
| `github_pr`     | Opens a GitHub pull request from a pushed branch — self-repo only |

All file tools are sandboxed to the active workspace root via
`WorkspaceManager::resolveSafe`, which rejects absolute paths and any
`../` traversal attempt.

## Self-improvement: letting Atlas open PRs against its own repo

`git` and `github_pr` are registered unconditionally, but they refuse
every call until you explicitly enable them:

```bash
export ATLAS_GITHUB_TOKEN=ghp_...   # fine-grained PAT, "Pull requests: write" on this repo only

./atlas \
  --self-repo=/path/to/your/clone/of/Atlas \
  --github-repo=your-org/Atlas
```

This registers a dedicated `self` workspace pointed at `--self-repo`. Both
tools check that the *exact* workspace root of the incoming request
matches `--self-repo` before doing anything — a request against
`workspace: "default"` (or any other workspace) gets a hard refusal, so a
normal chat session can never accidentally trigger a git push. To actually
use it, point requests at the self workspace:

```json
{
  "session_id": "self-improve-1",
  "workspace": "self",
  "message": "Read Agent.cpp, fix the bug where ..., then commit it to a feature branch and open a PR."
}
```

Typical flow the agent follows: `search_symbol`/`read_file` to find the
code → `edit_file` to change it → `git checkout_branch` (feature branch;
`main`/`master` are hard-blocked) → `git add` → `git commit` → `git push`
(refuses if somehow still on `main`) → `github_pr` to open the PR for a
human to review.

**Safety properties, by construction, not just prompting:**
- `git`/`github_pr` are inert (return a clear error) unless `--self-repo`
  is set, and further inert unless the *specific* incoming workspace
  matches it exactly.
- `checkout_branch` refuses `"main"`, `"master"`, and `"HEAD"` as targets.
- `push` re-checks the actual current branch via `git rev-parse` right
  before pushing and refuses if it's `main`/`master` — this isn't just a
  prompt instruction, it's checked in C++ regardless of what the model
  asked for.
- Every git invocation runs via `fork()`+`execvp()` with an explicit argv
  array — never a shell — so tool arguments can't be used for shell
  injection no matter what a model puts in them.
- `github_pr` only ever calls `POST /repos/{repo}/pulls` — it has no
  merge, close, or force-push capability at all, and only opens a PR
  against the single `--github-repo` slug it was started with.
- Nothing here auto-merges. A human still reviews and merges every PR.

**What this doesn't give you:** the agent cannot rebuild or restart
itself — editing `.cpp` files doesn't change the already-running binary,
and there's intentionally no `run_shell`/build tool here. Once a PR is
merged, a human (or a separate CI/CD pipeline) still needs to rebuild and
redeploy Atlas for the change to take effect. Wiring that up safely (who
approves a PR that changes Atlas's own safety checks, in particular)
is a deliberately separate, harder problem this project doesn't solve.

## Project layout

```
Atlas/
├── CMakeLists.txt
├── include/atlas/       # public headers, mirrors src/ layout
│   ├── core/             # Application, WorkspaceManager, SessionManager, SymbolIndexer
│   ├── storage/          # StorageManager interface + FileStorageManager
│   ├── tools/            # Tool interface, ToolManager, built-in tools
│   ├── agent/            # LLMClient (Ollama HTTP client), Agent (ReAct loop)
│   └── api/              # APIServer (cpp-httplib REST endpoint)
└── src/                  # implementations, same subfolders as above
```

## Notes / known limitations

- `SymbolIndexer` uses regex heuristics, not a real C++ parser — it's
  intentionally "good enough for jumping to a definition," not a full AST.
- `Agent::extractToolCalls` falls back to parsing a fenced ```json block
  out of `content` for models that don't reliably emit native
  `tool_calls`, which matters for smaller local models like `qwen3:4b`.
- The API server is synchronous/blocking (`httplib::Server::listen`); it
  runs on the main thread while a second thread watches for
  SIGINT/SIGTERM to trigger a clean shutdown.

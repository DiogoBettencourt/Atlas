# Atlas

**This file is actively maintained.**

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
  "reply": "...",
  "steps": [
    {"type": "assistant_thought", "content": "I'll check the file first."},
    {"type": "tool_call", "name": "read_file", "arguments": {"path": "hello.txt"}},
    {"type": "tool_result", "name": "read_file", "result": {"content": "...", "truncated": false}}
  ]
}
```
`steps` is the full trace of what the agent did to arrive at `reply` —
every tool call it made and every result it got back, in order. This
endpoint still blocks until the whole loop finishes; use `/chat/stream`
below if you want to see steps arrive live instead of all at once at the
end.

### `POST /chat/stream`
Same request body as `/chat`. Instead of one JSON object, the response is
newline-delimited JSON (NDJSON) — one `{"type": ...}` line per event,
written to the connection as the agent produces it, ending in a `final` or
`error` line:

```
{"type":"iteration_start","iteration":1,"max_iterations":20}
{"type":"assistant_thought","content":"I'll check the file first."}
{"type":"tool_call","name":"read_file","arguments":{"path":"hello.txt"}}
{"type":"tool_result","name":"read_file","result":{"content":"...","truncated":false}}
{"type":"iteration_start","iteration":2,"max_iterations":20}
{"type":"final","reply":"Done — the file says hello."}
```

**This only shows up live if your client reads the body incrementally.**
`Invoke-RestMethod` in PowerShell buffers the whole response before
returning anything, so it'll look identical to a slow `/chat` call. To see
it stream, either use `curl.exe` (ships with modern Windows):

```powershell
curl.exe -N -X POST http://127.0.0.1:8080/chat/stream `
  -H "Content-Type: application/json" `
  -d '{\"session_id\":\"s1\",\"message\":\"read hello.txt\"}'
```

or read the .NET `HttpClient` response stream directly in PowerShell:

```powershell
$body = '{"session_id":"s1","message":"read hello.txt"}'
$client = [System.Net.Http.HttpClient]::new()
$content = [System.Net.Http.StringContent]::new($body, [System.Text.Encoding]::UTF8, "application/json")
$response = $client.PostAsync("http://127.0.0.1:8080/chat/stream", $content).Result
$stream = $response.Content.ReadAsStreamAsync().Result
$reader = [System.IO.StreamReader]::new($stream)
while (-not $reader.EndOfStream) {
    Write-Host $reader.ReadLine()
}
```

## Built-in tools

Chat history is persisted per `session_id` as JSON under
`<data-dir>/sessions/<session_id>.json`, so conversations survive restarts.

The agent has seven tools available on every turn:

| Tool             | Purpose                                                        |
|------------------|------------------------------------------------------------------|
| `list_directory` | List a workspace-relative directory's immediate contents (one level) |
| `search_symbol`  | Find classes/functions/methods by name across the indexed workspace |
| `read_file`      | Read a workspace-relative file (truncated past 32KB)             |
| `write_file`     | Create or fully overwrite a workspace-relative file               |
| `edit_file`      | Exact, unique find-and-replace edit within an existing file       |
| `git`            | Restricted git ops (`status`/`diff`/`add`/`commit`/`checkout_branch`/`push`/`pull`) — self-repo only |
| `github_pr`      | Opens a GitHub pull request from a pushed branch — self-repo only |

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

Typical flow the agent follows: `git pull` to sync with the remote →
`list_directory`/`search_symbol`/`read_file` to find the code → `edit_file`
to change it → `git checkout_branch` (feature branch; `main`/`master` are
hard-blocked) → `git add` → `git commit` → `git push` (refuses if somehow
still on `main`) → `github_pr` to open the PR for a human to review.

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

## Specs

Longer-form guidance that didn't fit above:

- [`docs/specs/self-improvement-safety.md`](docs/specs/self-improvement-safety.md) —
  operational checklist for running `--self-repo` mode: dedicated clone,
  token scope, branch protection, what to scrutinize when reviewing a PR
  Atlas opened.
- [`docs/specs/local-gpu-inference.md`](docs/specs/local-gpu-inference.md) —
  running Ollama on an AMD GPU (ROCm) and sizing a model to fit in a given
  amount of VRAM.

# Atlas CLI

A thin terminal client for the [Atlas](../../README.md) agent - a
streaming REPL over Atlas's `POST /chat/stream` REST endpoint, styled
after tools like GitHub Copilot CLI: point it at a running Atlas server
and talk to the agent from your terminal instead of `curl`-ing NDJSON by
hand.

This is a v1-and-a-half scaffold: a working streaming client, a
functional Ink UI, a local config file, and a session picker - not a
polished product yet. See "What's not here yet" below.

## Requirements

- Node.js >= 18.17
- A running Atlas server (see the [root README](../../README.md#run)) -
  the CLI is a pure client, it doesn't embed or launch Atlas itself.

## Use

```bash
npm install
npm run dev -- --server http://127.0.0.1:8080
```

or, after `npm run build`:

```bash
npm start -- --server http://127.0.0.1:8080
```

| Flag                     | Default                      | Meaning                                             |
|---------------------------|--------------------------------|--------------------------------------------------------|
| `-s, --server <url>`     | `http://127.0.0.1:8080`, or the config file | Atlas API server base URL             |
| `-w, --workspace <name>` | `default`, or the config file  | Workspace name on the server                        |
| `--session <id>`         | -                              | Resume a specific session id, skipping the picker   |
| `--new`                  | -                              | Start a fresh session, skipping the picker          |
| `--list-sessions`        | -                              | Print known sessions for `--server`/`--workspace` and exit |

A flag always wins over the config file, which always wins over the
hardcoded default.

Type a message and press Enter. Tool calls and results stream in live as
the agent works; `/exit` or `/quit` (or Ctrl+C) quits.

### Config file

Written by hand for now (no `atlas config set` yet) at:

- Windows: `%APPDATA%\atlas-cli\config.json`
- Linux/macOS: `$XDG_CONFIG_HOME/atlas-cli/config.json` (or
  `~/.config/atlas-cli/config.json`)

```json
{
  "server": "http://192.168.1.50:8080",
  "workspace": "myproject"
}
```

Both fields are optional; either can be set independently. A malformed
file is reported as a warning on startup and ignored (falls back to
defaults) rather than crashing the CLI.

### Session picker

Starting the CLI without `--session`/`--new` shows a picker (↑↓ then
Enter) when there's at least one known session for the current
`--server`/`--workspace` - "start a new session" is always the first
option. On first run, or after `--list-sessions` shows nothing, there's
nothing to pick from yet, so it skips straight to a new session.

This is a local, client-side index only (next to the config file, at
`sessions.json` in the same directory) - it just remembers session ids
and a label (the session's first message) so you don't have to memorize
a UUID. It has no bearing on the actual conversation history, which
lives entirely on the Atlas server; deleting `sessions.json` only makes
the picker forget, it doesn't delete anything server-side.

## What's not here yet

- No retry/backoff if the connection drops mid-stream.
- `AtlasClient.chat()` (the non-streaming `/chat` endpoint) exists and is
  tested but isn't wired into the UI - `/chat/stream` is the only path
  the app actually uses.
- No `atlas config set` - the config file has to be hand-edited.
- No way to rename/delete a session from the picker itself (or from
  `--list-sessions`) - it just grows (bounded at the 50 most recently
  used) until entries age out.

## Development

```bash
npm run typecheck   # tsc --noEmit
npm test            # vitest - AtlasClient's NDJSON parsing against a
                     # real node:http server (chunked lines, dropped
                     # connections), the config/session-registry file
                     # I/O against a temp dir (ATLAS_CLI_CONFIG_DIR),
                     # SessionPicker's keyboard navigation, and an
                     # end-to-end App render test
npm run build        # emits dist/ for `npm start` / the `atlas` bin
```

Tests that touch the config/session files set `ATLAS_CLI_CONFIG_DIR` to a
temp directory rather than a real home directory - see
`src/config/paths.ts`.

`src/api/client.ts` is the one place that knows Atlas's wire format
(`/health`, `/chat`, `/chat/stream`) - AtlasUI is expected to share this
same client rather than reimplementing NDJSON parsing a second time, once
it exists.

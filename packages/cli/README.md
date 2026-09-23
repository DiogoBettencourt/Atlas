# Atlas CLI

A thin terminal client for the [Atlas](../../README.md) agent - a
streaming REPL over Atlas's `POST /chat/stream` REST endpoint, styled
after tools like GitHub Copilot CLI: point it at a running Atlas server
and talk to the agent from your terminal instead of `curl`-ing NDJSON by
hand.

This is a v1 scaffold: a working streaming client and a functional Ink
UI, not a polished product yet. See "What's not here yet" below.

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

| Flag                     | Default                  | Meaning                                       |
|---------------------------|----------------------------|------------------------------------------------|
| `-s, --server <url>`     | `http://127.0.0.1:8080`  | Atlas API server base URL                     |
| `-w, --workspace <name>` | `default`                | Workspace name on the server                  |
| `--session <id>`         | a fresh random UUID       | Session id to resume an existing conversation |

Type a message and press Enter. Tool calls and results stream in live as
the agent works; `/exit` or `/quit` (or Ctrl+C) quits.

## What's not here yet

- No persisted session picker - `--session` has to be typed by hand to
  resume a specific one.
- No local config file for a default `--server`/`--workspace`.
- No retry/backoff if the connection drops mid-stream.
- `AtlasClient.chat()` (the non-streaming `/chat` endpoint) exists and is
  tested but isn't wired into the UI - `/chat/stream` is the only path
  the app actually uses.

## Development

```bash
npm run typecheck   # tsc --noEmit
npm test            # vitest - exercises AtlasClient's NDJSON parsing
                     # against a real node:http server, including a
                     # deliberately chunk-split line and a dropped
                     # connection
npm run build        # emits dist/ for `npm start` / the `atlas` bin
```

`src/api/client.ts` is the one place that knows Atlas's wire format
(`/health`, `/chat`, `/chat/stream`) - AtlasUI is expected to share this
same client rather than reimplementing NDJSON parsing a second time, once
it exists.

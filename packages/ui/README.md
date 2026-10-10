# AtlasUI

A local web client for Atlas: streaming chat that shows the agent's thinking
and tool calls as they happen, and a sidebar of your sessions. Black and
red, and it follows your system's light/dark setting.

It is a plain static app (React + TypeScript, built with Vite) that talks to
the Atlas REST API. There is nothing to run besides Atlas itself.

## Use it

Build it once, then start Atlas from the repository root:

```bash
cd packages/ui
npm ci
npm run build
cd ../..

./build/atlas        # on Windows: .\build\Release\atlas.exe
```

Open <http://127.0.0.1:8080/ui/>. Atlas serves the `dist/` folder it finds at
`./packages/ui/dist`; pass `--ui-dir=<folder>` to point somewhere else. If the
folder doesn't exist, Atlas starts normally and the startup banner says the UI
isn't served.

## Develop it

```bash
npm run dev          # http://localhost:5173/ui/, talks to Atlas on :8080
npm test             # vitest + Testing Library
npm run typecheck
```

In dev the page and the API are on different origins. The API address defaults
to `http://127.0.0.1:8080`; override it with `VITE_ATLAS_URL` or by adding
`?server=http://host:port` to the page URL. Atlas allows cross-origin requests,
so no proxy is needed.

## How it fits together

- `@atlas/client` is AtlasCLI's `AtlasClient` (`packages/cli/src/api/client.ts`),
  imported by source through a Vite alias, so the CLI and the UI share one
  implementation of the wire format. It is plain `fetch` code. Turning it into a
  published package is tracked in #19.
- `src/chatState.ts` holds the pure logic: how each `/chat/stream` event folds
  into a turn, and how a saved session's history becomes a transcript again
  (tool calls included).
- The open session is kept in the URL (`#/s/<id>`), so a reload comes back to it.
- The UI asks `/chat/stream` for `stream_deltas`, so the model's thinking and
  reply appear as they are generated, one piece at a time. `chatState.ts` grows
  a live block from the pieces and swaps in the complete text when it arrives.
- Sessions come from `GET /sessions`; opening one loads
  `GET /sessions/:id/history`; deleting one calls `DELETE /sessions/:id` after a
  confirmation.

## Known limitations

- **No Stop button yet.** Atlas can't cancel a turn that is already running, so
  the UI waits for it to finish rather than pretend to stop it.
- Thinking isn't saved in a session's history, so it shows live but not when you
  reopen a session.
- Assistant text supports code blocks and `inline code` only, not full Markdown.
- One workspace (`default`); a workspace picker isn't built yet.

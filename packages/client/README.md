# @atlas/client

The one typed client for [Atlas's REST API](../../README.md#api): health and
server info, one-shot and streaming chat (`/chat`, `/chat/stream` with live
`thinking_delta` / `content_delta` text), sessions (list, history, delete) and
stopping a running turn (`cancel`). It knows Atlas's wire format and nothing
else, so AtlasCLI and AtlasUI share one tested implementation instead of
keeping two copies of the NDJSON parsing.

It is plain `fetch` code with no Node-only imports, so it runs in a terminal and
in a browser.

```ts
import { AtlasClient } from "@atlas/client";

const client = new AtlasClient({ baseUrl: "http://127.0.0.1:8080" });
const reply = await client.sendMessage(
  { sessionId: "demo", message: "hello", streamDeltas: true },
  (event) => console.log(event.type)
);
```

## How the packages use it

This package is **not published**. It is consumed as source:

- **AtlasUI** aliases `@atlas/client` to `packages/client/src/index.ts` in
  `vite.config.ts` and `tsconfig.json`.
- **AtlasCLI** does the same for its type check and tests, and its build
  (`scripts/build.mjs`, esbuild) inlines the client into `dist/cli.js`, so the
  npm package stays a single self-contained install.

## Development

```
npm ci
npm run typecheck
npm test
```

The tests run the client against a real `node:http` server (not a mocked
`fetch`), since parsing bytes off a chunked response is the point.

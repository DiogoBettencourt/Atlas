// End-to-end integration test for the UI layer: renders the real App
// component wired to a real AtlasClient talking to a real local
// node:http server (not a mocked fetch or a fake client), simulates
// typing a message and pressing Enter, and asserts the streamed reply
// lands in the rendered transcript. This is the one test that exercises
// App + AtlasClient together the way the actual binary does.
import { createServer, type Server } from "node:http";
import type { AddressInfo } from "node:net";
import { render } from "ink-testing-library";
import React from "react";
import { describe, expect, it } from "vitest";
import { AtlasClient } from "../api/client.js";
import App from "./App.js";

async function startNdjsonServer(lines: object[]): Promise<{ baseUrl: string; server: Server }> {
  const server = createServer((_req, res) => {
    res.writeHead(200, { "Content-Type": "application/x-ndjson" });
    for (const line of lines) res.write(JSON.stringify(line) + "\n");
    res.end();
  });
  await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", resolve));
  const { port } = server.address() as AddressInfo;
  return { baseUrl: `http://127.0.0.1:${port}`, server };
}

function wait(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

describe("App", () => {
  it("renders a submitted message and the streamed reply in the transcript", async () => {
    const { baseUrl, server } = await startNdjsonServer([
      { type: "iteration_start", iteration: 1, max_iterations: 20 },
      { type: "tool_call", name: "read_file", arguments: { path: "hello.txt" } },
      { type: "final", reply: "the file says hi" },
    ]);

    try {
      const client = new AtlasClient({ baseUrl });
      const { stdin, lastFrame, unmount } = render(
        React.createElement(App, {
          client,
          sessionId: "test-session",
          workspace: "default",
          serverLabel: baseUrl,
        })
      );

      // Give Ink's effect that subscribes to stdin a tick to run before
      // writing - writing synchronously right after render() can race
      // ahead of that subscription and be silently dropped.
      await wait(50);
      stdin.write("read hello.txt");
      await wait(20);
      stdin.write("\r");

      // Poll instead of a fixed sleep: this is a real localhost network
      // round trip, not a fake timer, so completion time isn't
      // deterministic.
      let frame = lastFrame() ?? "";
      for (let i = 0; i < 50 && !frame.includes("the file says hi"); i++) {
        await wait(20);
        frame = lastFrame() ?? "";
      }

      expect(frame).toContain("you> read hello.txt");
      expect(frame).toContain("atlas> the file says hi");

      unmount();
    } finally {
      server.close();
    }
  });
});

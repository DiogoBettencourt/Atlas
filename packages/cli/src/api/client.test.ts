// Tests AtlasClient against a real node:http server rather than a mocked
// fetch - the whole point of this client is correctly parsing bytes
// coming off a real chunked HTTP response, and a mocked fetch would just
// hand back whatever shape we told it to, defeating the purpose.
import { createServer, type IncomingMessage, type Server, type ServerResponse } from "node:http";
import type { AddressInfo } from "node:net";
import { describe, expect, it } from "vitest";
import { AtlasApiError, AtlasClient, type AgentEvent } from "./client.js";

type Handler = (req: IncomingMessage, res: ServerResponse) => void;

async function withServer(handler: Handler, run: (baseUrl: string) => Promise<void>): Promise<void> {
  const server: Server = createServer(handler);
  await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", resolve));
  const { port } = server.address() as AddressInfo;
  try {
    await run(`http://127.0.0.1:${port}`);
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
  }
}

describe("AtlasClient.health", () => {
  it("returns true when the server reports ok", async () => {
    await withServer(
      (_req, res) => {
        res.writeHead(200, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ status: "ok" }));
      },
      async (baseUrl) => {
        const client = new AtlasClient({ baseUrl });
        await expect(client.health()).resolves.toBe(true);
      }
    );
  });

  it("returns false rather than throwing when the server is unreachable", async () => {
    const client = new AtlasClient({ baseUrl: "http://127.0.0.1:1" });
    await expect(client.health()).resolves.toBe(false);
  });
});

describe("AtlasClient.chat", () => {
  it("sends session_id/message/workspace and parses the response shape", async () => {
    await withServer(
      (req, res) => {
        let raw = "";
        req.on("data", (chunk) => (raw += chunk));
        req.on("end", () => {
          expect(JSON.parse(raw)).toEqual({ session_id: "s1", message: "hi", workspace: "default" });
          res.writeHead(200, { "Content-Type": "application/json" });
          res.end(JSON.stringify({ session_id: "s1", reply: "hello back", steps: [] }));
        });
      },
      async (baseUrl) => {
        const client = new AtlasClient({ baseUrl });
        const result = await client.chat({ sessionId: "s1", message: "hi", workspace: "default" });
        expect(result).toEqual({ sessionId: "s1", reply: "hello back", steps: [] });
      }
    );
  });

  it("throws AtlasApiError carrying the server's error message on a non-2xx response", async () => {
    await withServer(
      (_req, res) => {
        res.writeHead(500, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ error: "ollama unreachable" }));
      },
      async (baseUrl) => {
        const client = new AtlasClient({ baseUrl });
        await expect(client.chat({ sessionId: "s1", message: "hi" })).rejects.toThrow("ollama unreachable");
      }
    );
  });
});

describe("AtlasClient.chatStream", () => {
  it("parses every NDJSON line in order and resolves with the final reply", async () => {
    const lines: AgentEvent[] = [
      { type: "iteration_start", iteration: 1, max_iterations: 20 },
      { type: "assistant_thought", content: "checking the file" },
      { type: "tool_call", name: "read_file", arguments: { path: "hello.txt" } },
      { type: "tool_result", name: "read_file", result: { content: "hi" } },
      { type: "final", reply: "the file says hi" },
    ];

    await withServer(
      (_req, res) => {
        res.writeHead(200, { "Content-Type": "application/x-ndjson" });
        for (const line of lines) res.write(JSON.stringify(line) + "\n");
        res.end();
      },
      async (baseUrl) => {
        const client = new AtlasClient({ baseUrl });
        const received: AgentEvent[] = [];
        const reply = await client.chatStream({ sessionId: "s1", message: "read hello.txt" }, (event) =>
          received.push(event)
        );
        expect(received).toEqual(lines);
        expect(reply).toBe("the file says hi");
      }
    );
  });

  it("rejects with the error event's message when the stream ends on an error line", async () => {
    await withServer(
      (_req, res) => {
        res.writeHead(200, { "Content-Type": "application/x-ndjson" });
        res.write(JSON.stringify({ type: "iteration_start", iteration: 1, max_iterations: 20 }) + "\n");
        res.write(JSON.stringify({ type: "error", message: "model timed out" }) + "\n");
        res.end();
      },
      async (baseUrl) => {
        const client = new AtlasClient({ baseUrl });
        await expect(client.chatStream({ sessionId: "s1", message: "hi" }, () => {})).rejects.toThrow(
          "model timed out"
        );
      }
    );
  });

  it("rejects if the connection closes with neither a final nor an error line", async () => {
    await withServer(
      (_req, res) => {
        res.writeHead(200, { "Content-Type": "application/x-ndjson" });
        res.write(JSON.stringify({ type: "iteration_start", iteration: 1, max_iterations: 20 }) + "\n");
        res.end();
      },
      async (baseUrl) => {
        const client = new AtlasClient({ baseUrl });
        await expect(client.chatStream({ sessionId: "s1", message: "hi" }, () => {})).rejects.toThrow(
          AtlasApiError
        );
      }
    );
  });

  it("still parses correctly when a line is split across two TCP chunks", async () => {
    await withServer(
      (_req, res) => {
        res.writeHead(200, { "Content-Type": "application/x-ndjson" });
        const line = JSON.stringify({ type: "final", reply: "chunked ok" });
        res.write(line.slice(0, 10));
        setTimeout(() => {
          res.write(line.slice(10) + "\n");
          res.end();
        }, 10);
      },
      async (baseUrl) => {
        const client = new AtlasClient({ baseUrl });
        const received: AgentEvent[] = [];
        const reply = await client.chatStream({ sessionId: "s1", message: "hi" }, (event) =>
          received.push(event)
        );
        expect(reply).toBe("chunked ok");
        expect(received).toEqual([{ type: "final", reply: "chunked ok" }]);
      }
    );
  });

  it("rejects if the server never connects at all", async () => {
    const client = new AtlasClient({ baseUrl: "http://127.0.0.1:1" });
    await expect(client.chatStream({ sessionId: "s1", message: "hi" }, () => {})).rejects.toThrow();
  });
});

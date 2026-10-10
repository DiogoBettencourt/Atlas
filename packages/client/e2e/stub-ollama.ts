// A stand-in for Ollama, just enough of POST /api/chat for the end-to-end
// tests: what it answers depends on the last user message, so each test
// can steer the real atlas binary through the path it wants.
//
//   "list files"  -> asks for the list_directory tool; once it sees the tool's
//                    result, answers with it (so a reply that mentions the
//                    file proves the tool really ran inside atlas)
//   "slow"        -> keeps "thinking" for ~10 seconds, until atlas hangs up
//   anything else -> a little thinking, then "Hello there"
import { createServer, type IncomingMessage, type Server, type ServerResponse } from "node:http";
import type { AddressInfo } from "node:net";

export interface StubRequest {
  stream: boolean;
  messages: Array<{ role: string; content?: string; tool_calls?: unknown[]; thinking?: string }>;
  tools: number;
}

export interface StubOllama {
  port: number;
  requests: StubRequest[];
  // Resolves once atlas closes a connection while a "slow" reply was streaming.
  clientLeft: Promise<void>;
  close(): Promise<void>;
}

const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

function line(message: Record<string, unknown>, done = false): string {
  return JSON.stringify({ model: "stub", message: { role: "assistant", ...message }, done }) + "\n";
}

export async function startStubOllama(): Promise<StubOllama> {
  const requests: StubRequest[] = [];
  let markClientLeft: () => void = () => {};
  const clientLeft = new Promise<void>((resolve) => (markClientLeft = resolve));

  async function chat(req: IncomingMessage, res: ServerResponse) {
    let raw = "";
    for await (const chunk of req) raw += chunk;
    const body = JSON.parse(raw) as { stream?: boolean; messages: StubRequest["messages"]; tools?: unknown[] };
    const stream = body.stream !== false;
    requests.push({ stream, messages: body.messages, tools: body.tools?.length ?? 0 });

    const last = body.messages[body.messages.length - 1];
    const lastUser = [...body.messages].reverse().find((m) => m.role === "user")?.content ?? "";

    let pieces: Array<Record<string, unknown>>;
    if (last.role === "tool") {
      pieces = [{ content: `The workspace contains: ${last.content}` }];
    } else if (lastUser.includes("list files")) {
      pieces = [
        { thinking: "I should look at the directory. " },
        { content: "", tool_calls: [{ function: { name: "list_directory", arguments: { path: "." } } }] },
      ];
    } else if (lastUser.includes("slow")) {
      res.writeHead(200, { "Content-Type": "application/x-ndjson" });
      let open = true;
      res.on("close", () => {
        open = false;
        markClientLeft();
      });
      for (let i = 0; i < 300 && open; i++) {
        res.write(line({ thinking: `thought ${i}. ` }));
        await sleep(30);
      }
      return res.end();
    } else {
      pieces = [{ thinking: "Considering. " }, { thinking: "Done thinking. " }, { content: "Hello " }, { content: "there" }];
    }

    if (!stream) {
      const whole = {
        role: "assistant",
        content: pieces.map((p) => (typeof p.content === "string" ? p.content : "")).join(""),
        thinking: pieces.map((p) => (typeof p.thinking === "string" ? p.thinking : "")).join("") || undefined,
        tool_calls: pieces.flatMap((p) => (Array.isArray(p.tool_calls) ? p.tool_calls : [])),
      };
      if (whole.tool_calls.length === 0) delete (whole as { tool_calls?: unknown }).tool_calls;
      res.writeHead(200, { "Content-Type": "application/json" });
      return res.end(JSON.stringify({ model: "stub", message: whole, done: true }));
    }

    res.writeHead(200, { "Content-Type": "application/x-ndjson" });
    for (const piece of pieces) {
      res.write(line(piece));
      await sleep(10);
    }
    res.end(line({ content: "" }, true));
  }

  const server: Server = createServer((req, res) => {
    if (req.url === "/api/chat" && req.method === "POST") {
      chat(req, res).catch(() => res.destroy());
      return;
    }
    res.writeHead(404);
    res.end();
  });
  await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", resolve));

  return {
    port: (server.address() as AddressInfo).port,
    requests,
    clientLeft,
    close: () =>
      new Promise<void>((resolve) => {
        server.closeAllConnections();
        server.close(() => resolve());
      }),
  };
}

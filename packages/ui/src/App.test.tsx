// Drives the whole app against a fake Atlas on a real HTTP socket, so the
// real AtlasClient (NDJSON parsing and all) is what the UI talks to.
import { AtlasClient, type AgentEvent } from "@atlas/client";
import { fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { createServer, type IncomingMessage, type Server, type ServerResponse } from "node:http";
import type { AddressInfo } from "node:net";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import App from "./App";

interface Row {
  id: string;
  title: string;
  message_count: number;
  updated_at: string;
}

type Step = AgentEvent | "PAUSE";

class FakeAtlas {
  server!: Server;
  baseUrl = "";
  healthy = true;
  sessions: Row[] = [];
  histories: Record<string, unknown[]> = {};
  deleted: string[] = [];
  chatBodies: Array<{ session_id: string; message: string; stream_deltas?: boolean }> = [];
  script: Step[] = [{ type: "final", reply: "ok" }];
  // Each "PAUSE" in a script holds the stream until the test calls release().
  private waiting: Array<() => void> = [];

  async release() {
    for (let i = 0; i < 200 && this.waiting.length === 0; i++) {
      await new Promise((r) => setTimeout(r, 5));
    }
    this.waiting.shift()?.();
  }

  async start() {
    this.server = createServer((req, res) => void this.handle(req, res));
    await new Promise<void>((r) => this.server.listen(0, "127.0.0.1", r));
    this.baseUrl = `http://127.0.0.1:${(this.server.address() as AddressInfo).port}`;
  }
  async stop() {
    this.server.closeAllConnections();
    await new Promise<void>((r) => this.server.close(() => r()));
  }

  private json(res: ServerResponse, body: unknown, status = 200) {
    res.writeHead(status, { "Content-Type": "application/json" });
    res.end(JSON.stringify(body));
  }

  private async handle(req: IncomingMessage, res: ServerResponse) {
    const url = req.url ?? "";
    if (url === "/health") {
      return this.healthy
        ? this.json(res, { status: "ok", version: "9.9.9", backend: "ollama", model: "test-model:1b" })
        : this.json(res, { error: "down" }, 503);
    }
    if (url === "/sessions" && req.method === "GET") return this.json(res, { sessions: this.sessions });
    const history = url.match(/^\/sessions\/([^/]+)\/history$/);
    if (history) return this.json(res, { session_id: history[1], messages: this.histories[history[1]] ?? [] });
    const del = url.match(/^\/sessions\/([^/]+)$/);
    if (del && req.method === "DELETE") {
      this.deleted.push(decodeURIComponent(del[1]));
      this.sessions = this.sessions.filter((s) => s.id !== del[1]);
      return this.json(res, { session_id: del[1], deleted: true });
    }
    if (url === "/chat/stream" && req.method === "POST") {
      let raw = "";
      for await (const chunk of req) raw += chunk;
      const body = JSON.parse(raw) as { session_id: string; message: string; stream_deltas?: boolean };
      this.chatBodies.push(body);
      res.writeHead(200, { "Content-Type": "application/x-ndjson" });
      for (const step of this.script) {
        if (step === "PAUSE") {
          await new Promise<void>((resolve) => this.waiting.push(resolve));
          continue;
        }
        res.write(JSON.stringify(step) + "\n");
        await new Promise((r) => setTimeout(r, 5));
      }
      // Once the chat finishes the session exists server-side.
      if (!this.sessions.some((s) => s.id === body.session_id)) {
        this.sessions.unshift({
          id: body.session_id,
          title: body.message,
          message_count: 2,
          updated_at: new Date().toISOString(),
        });
      }
      return res.end();
    }
    res.writeHead(404);
    res.end();
  }
}

let fake: FakeAtlas;

beforeEach(async () => {
  window.location.hash = "";
  fake = new FakeAtlas();
  await fake.start();
});
afterEach(async () => {
  await fake.stop();
});

// Types into the composer and sends with Enter once the app is ready for it
// (it holds Send back until the session's history has loaded).
async function sendMessage(text: string) {
  const box = await screen.findByLabelText("Message Atlas");
  fireEvent.change(box, { target: { value: text } });
  const send = (await screen.findByRole("button", { name: "Send" })) as HTMLButtonElement;
  await waitFor(() => expect(send.disabled).toBe(false));
  fireEvent.keyDown(box, { key: "Enter" });
}

function renderApp() {
  return render(<App client={new AtlasClient({ baseUrl: fake.baseUrl })} />);
}

describe("AtlasUI", () => {
  it("shows the empty state and an empty session list on a fresh install", async () => {
    renderApp();
    expect(await screen.findByText("What are we working on?")).toBeTruthy();
    expect(await screen.findByText("Not saved yet")).toBeTruthy();
    expect(screen.getByText(/Connected/)).toBeTruthy();
    // What the server says about itself shows up in the header and footer.
    expect(await screen.findByText("test-model:1b")).toBeTruthy();
    expect(screen.getByText("v9.9.9")).toBeTruthy();
  });

  it("lists sessions and restores a transcript, tool calls included, when one is opened", async () => {
    fake.sessions = [
      { id: "s1", title: "read hello.txt", message_count: 2, updated_at: new Date().toISOString() },
      { id: "s2", title: "second chat", message_count: 2, updated_at: "2026-01-01T00:00:00Z" },
    ];
    fake.histories.s1 = [
      { role: "user", content: "read hello.txt" },
      { role: "assistant", content: "", tool_calls: [{ function: { name: "read_file", arguments: { path: "hello.txt" } } }] },
      { role: "tool", name: "read_file", content: '{"content":"hello"}' },
      { role: "assistant", content: "The file says hello." },
    ];
    renderApp();

    const nav = await screen.findByRole("navigation", { name: "Sessions" });
    expect(await within(nav).findByText("read hello.txt")).toBeTruthy();
    expect(within(nav).getByText("second chat")).toBeTruthy();

    fireEvent.click(within(nav).getByText("read hello.txt"));
    expect(await screen.findByText("The file says hello.")).toBeTruthy();
    expect(screen.getByText("read_file")).toBeTruthy();
    expect(screen.getByText('path: "hello.txt"')).toBeTruthy();
    expect(screen.getByTestId("user-turn").textContent).toBe("read hello.txt");
    expect(window.location.hash).toBe("#/s/s1");
  });

  it("streams a turn live: thinking and a running tool call first, then the result and answer", async () => {
    fake.script = [
      { type: "iteration_start", iteration: 1, max_iterations: 20 },
      { type: "thinking", content: "I should read the file first." },
      { type: "tool_call", name: "read_file", arguments: { path: "ROADMAP.md" } },
      "PAUSE",
      { type: "tool_result", name: "read_file", result: { content: "# Roadmap" } },
      { type: "iteration_start", iteration: 2, max_iterations: 20 },
      { type: "final", reply: "Next up is AtlasUI." },
    ];
    renderApp();
    await screen.findByText("What are we working on?");

    await sendMessage("what is next?");

    // Mid-stream: the question, the reasoning and a tool that is still running.
    expect(await screen.findByText("I should read the file first.")).toBeTruthy();
    expect(await screen.findByText("Running")).toBeTruthy();
    expect(screen.getByText("path: \"ROADMAP.md\"")).toBeTruthy();
    expect(screen.getByRole("button", { name: "Working…" }).hasAttribute("disabled")).toBe(true);
    expect(screen.queryByText("Next up is AtlasUI.")).toBeNull();

    await fake.release();
    expect(await screen.findByText("Next up is AtlasUI.")).toBeTruthy();
    expect(screen.getByText("Done")).toBeTruthy();
    expect(screen.queryByText("Running")).toBeNull();
    expect(fake.chatBodies).toHaveLength(1);
    expect(fake.chatBodies[0].message).toBe("what is next?");

    // The new session appears in the sidebar once the turn is over.
    const nav = screen.getByRole("navigation", { name: "Sessions" });
    expect(await within(nav).findByText("what is next?")).toBeTruthy();
    await waitFor(() => expect(screen.getByRole("button", { name: "Send" })).toBeTruthy());
  });

  it("shows thinking and the reply as they are generated, before the step finishes", async () => {
    fake.script = [
      { type: "iteration_start", iteration: 1, max_iterations: 20 },
      { type: "thinking_delta", content: "Let me " },
      { type: "thinking_delta", content: "consider this." },
      "PAUSE",
      { type: "content_delta", content: "Hel" },
      { type: "content_delta", content: "lo there" },
      "PAUSE",
      { type: "thinking", content: "Let me consider this." },
      { type: "final", reply: "Hello there" },
    ];
    renderApp();
    await sendMessage("hi");

    // The step is not over (the model is still generating) but its reasoning is already on screen.
    expect(await screen.findByText("Let me consider this.")).toBeTruthy();
    expect(screen.queryByText("Hello there")).toBeNull();
    expect(screen.getByRole("button", { name: "Working…" })).toBeTruthy();

    await fake.release();
    expect(await screen.findByText("Hello there")).toBeTruthy();
    // Still streaming: the complete answer has not arrived yet.
    expect(screen.getByRole("button", { name: "Working…" })).toBeTruthy();

    await fake.release();
    await waitFor(() => expect(screen.getByRole("button", { name: "Send" })).toBeTruthy());
    // One thinking block and one reply block, not duplicates of each.
    expect(screen.getAllByText("Let me consider this.")).toHaveLength(1);
    expect(screen.getAllByText("Hello there")).toHaveLength(1);
    expect(fake.chatBodies[0].stream_deltas).toBe(true);
  });

  it("does not send on Shift+Enter, and ignores an empty message", async () => {
    renderApp();
    const box = (await screen.findByLabelText("Message Atlas")) as HTMLTextAreaElement;
    const send = (await screen.findByRole("button", { name: "Send" })) as HTMLButtonElement;
    fireEvent.change(box, { target: { value: "line one" } });
    await waitFor(() => expect(send.disabled).toBe(false));
    fireEvent.keyDown(box, { key: "Enter", shiftKey: true });
    fireEvent.change(box, { target: { value: "   " } });
    fireEvent.keyDown(box, { key: "Enter" });
    expect(fake.chatBodies).toHaveLength(0);
  });

  it("sends a starter prompt from the empty state", async () => {
    renderApp();
    const starter = await screen.findByRole("button", { name: "Find TODOs in src/" });
    await waitFor(() => expect((starter as HTMLButtonElement).disabled).toBe(false));
    fireEvent.click(starter);
    expect(await screen.findByText("ok")).toBeTruthy();
    expect(fake.chatBodies[0].message).toBe("Find TODOs in src/");
  });

  it("shows a server error inside the assistant turn", async () => {
    fake.script = [{ type: "error", message: "model not found" }];
    renderApp();
    await sendMessage("hi");
    const alert = await screen.findByText("model not found");
    expect(alert.getAttribute("class")).toContain("error");
    // The composer is usable again afterwards.
    await waitFor(() => expect(screen.getByRole("button", { name: "Send" })).toBeTruthy());
  });

  it("deletes a session only after confirming, and Cancel keeps it", async () => {
    fake.sessions = [
      { id: "s1", title: "keep me", message_count: 2, updated_at: new Date().toISOString() },
      { id: "s2", title: "remove me", message_count: 2, updated_at: new Date().toISOString() },
    ];
    renderApp();
    const nav = await screen.findByRole("navigation", { name: "Sessions" });
    await within(nav).findByText("remove me");

    fireEvent.click(within(nav).getByRole("button", { name: "Delete session: remove me" }));
    let dialog = await screen.findByRole("dialog");
    expect(within(dialog).getByText(/remove me/)).toBeTruthy();
    expect(document.activeElement).toBe(within(dialog).getByRole("button", { name: "Cancel" }));
    fireEvent.click(within(dialog).getByRole("button", { name: "Cancel" }));
    expect(screen.queryByRole("dialog")).toBeNull();
    expect(fake.deleted).toEqual([]);

    fireEvent.click(within(nav).getByRole("button", { name: "Delete session: remove me" }));
    dialog = await screen.findByRole("dialog");
    fireEvent.click(within(dialog).getByRole("button", { name: "Delete session" }));
    await waitFor(() => expect(within(nav).queryByText("remove me")).toBeNull());
    expect(fake.deleted).toEqual(["s2"]);
    expect(within(nav).getByText("keep me")).toBeTruthy();
  });

  it("closes the delete dialog with Escape", async () => {
    fake.sessions = [{ id: "s1", title: "a chat", message_count: 2, updated_at: new Date().toISOString() }];
    renderApp();
    const nav = await screen.findByRole("navigation", { name: "Sessions" });
    fireEvent.click(await within(nav).findByRole("button", { name: "Delete session: a chat" }));
    const dialog = await screen.findByRole("dialog");
    fireEvent.keyDown(dialog, { key: "Escape" });
    expect(screen.queryByRole("dialog")).toBeNull();
    expect(fake.deleted).toEqual([]);
  });

  it("starts a fresh chat after deleting the session that was open", async () => {
    fake.sessions = [{ id: "s1", title: "open one", message_count: 2, updated_at: new Date().toISOString() }];
    fake.histories.s1 = [
      { role: "user", content: "open one" },
      { role: "assistant", content: "reply" },
    ];
    renderApp();
    const nav = await screen.findByRole("navigation", { name: "Sessions" });
    fireEvent.click(await within(nav).findByText("open one"));
    await screen.findByText("reply");

    fireEvent.click(within(nav).getByRole("button", { name: "Delete session: open one" }));
    fireEvent.click(within(await screen.findByRole("dialog")).getByRole("button", { name: "Delete session" }));
    expect(await screen.findByText("What are we working on?")).toBeTruthy();
    expect(window.location.hash).not.toBe("#/s/s1");
  });

  it("says so when Atlas can't be reached", async () => {
    fake.healthy = false;
    renderApp();
    expect(await screen.findByText(/Can't reach Atlas/)).toBeTruthy();
    expect(screen.getByText(/Offline/)).toBeTruthy();
    // You can still type a draft, but Send is held back until Atlas is reachable.
    fireEvent.change(screen.getByLabelText("Message Atlas"), { target: { value: "hello?" } });
    expect((screen.getByRole("button", { name: "Send" }) as HTMLButtonElement).disabled).toBe(true);
  });
});

// End-to-end: the real atlas binary, talking to a stub Ollama, driven by the
// real AtlasClient over real HTTP. It catches what the C++ tests and the
// client tests can't see alone - a request the client sends that the server
// reads differently, an event the server writes that the client can't parse.
//
// Needs the built binary:
//   ATLAS_BIN=../../build/atlas npm run test:e2e        (build\Release\atlas.exe on Windows)
import { execFile, spawn, type ChildProcess } from "node:child_process";
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { createServer } from "node:net";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { promisify } from "node:util";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { AtlasApiError, AtlasClient, type AgentEvent } from "../src/index.js";
import { startStubOllama, type StubOllama } from "./stub-ollama.js";

const run = promisify(execFile);
const atlasBin = process.env.ATLAS_BIN ? resolve(process.env.ATLAS_BIN) : undefined;

async function freePort(): Promise<number> {
  return new Promise((done, fail) => {
    const probe = createServer();
    probe.once("error", fail);
    probe.listen(0, "127.0.0.1", () => {
      const { port } = probe.address() as { port: number };
      probe.close(() => done(port));
    });
  });
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

async function waitFor(check: () => boolean | Promise<boolean>, what: string, ms = 10_000): Promise<void> {
  const end = Date.now() + ms;
  while (Date.now() < end) {
    if (await check()) return;
    await sleep(25);
  }
  throw new Error(`timed out waiting for ${what}`);
}

const types = (events: AgentEvent[]) => events.map((e) => e.type);

describe.skipIf(!atlasBin)("atlas (real binary) end to end", () => {
  let tmp: string;
  let stub: StubOllama;
  let atlas: ChildProcess;
  let output = "";
  let client: AtlasClient;
  let baseUrl: string;

  beforeAll(async () => {
    tmp = mkdtempSync(join(tmpdir(), "atlas-e2e-"));
    stub = await startStubOllama();

    // A workspace with one file in it, for the list_directory tool to find.
    const workspace = join(tmp, "workspaces", "default");
    mkdirSync(workspace, { recursive: true });
    writeFileSync(join(workspace, "hello-e2e.txt"), "hi");
    // A built UI to serve.
    const uiDir = join(tmp, "ui");
    mkdirSync(uiDir);
    writeFileSync(join(uiDir, "index.html"), "<!doctype html><title>e2e ui</title>");

    const port = await freePort();
    baseUrl = `http://127.0.0.1:${port}`;
    atlas = spawn(
      atlasBin!,
      [
        `--port=${port}`,
        "--model=e2e-model",
        `--ollama-port=${stub.port}`,
        `--data-dir=${join(tmp, "storage")}`,
        `--workspaces-dir=${join(tmp, "workspaces")}`,
        `--ui-dir=${uiDir}`,
      ],
      { cwd: tmp, stdio: ["ignore", "pipe", "pipe"] }
    );
    atlas.stdout?.on("data", (d) => (output += d));
    atlas.stderr?.on("data", (d) => (output += d));
    let exited = false;
    atlas.once("exit", () => (exited = true));

    client = new AtlasClient({ baseUrl });
    try {
      await waitFor(async () => !exited && (await client.health()), "atlas to answer /health", 20_000);
    } catch (err) {
      throw new Error(`${(err as Error).message}\n--- atlas output ---\n${output}`);
    }
  });

  afterAll(async () => {
    atlas?.kill();
    await stub?.close();
    if (tmp) rmSync(tmp, { recursive: true, force: true });
  });

  it("reports itself on /health: version, backend and model", async () => {
    const info = await client.info();
    expect(info).toMatchObject({ backend: "ollama", model: "e2e-model" });
    expect(info?.version).toMatch(/^\d+\.\d+\.\d+$/);
  });

  it("prints its version", async () => {
    const { stdout } = await run(atlasBin!, ["--version"]);
    expect(stdout).toMatch(/\d+\.\d+\.\d+/);
  });

  it("answers a one-shot chat", async () => {
    const reply = await client.chat({ sessionId: "e2e-plain", message: "hi" });
    expect(reply.reply).toBe("Hello there");
    // The steps are what happened on the way to the reply (here, the model's thinking).
    expect(types(reply.steps)).toEqual(["thinking"]);
  });

  it("streams a turn without live text unless asked (what AtlasCLI sees)", async () => {
    const events: AgentEvent[] = [];
    const reply = await client.sendMessage({ sessionId: "e2e-quiet", message: "hi" }, (e) => events.push(e));
    expect(reply).toBe("Hello there");
    expect(types(events)).toEqual(["iteration_start", "thinking", "final"]);
  });

  it("streams thinking and reply live when asked, then the complete events (what AtlasUI sees)", async () => {
    const events: AgentEvent[] = [];
    const before = stub.requests.length;
    const reply = await client.sendMessage({ sessionId: "e2e-live", message: "hi", streamDeltas: true }, (e) => events.push(e));
    expect(reply).toBe("Hello there");

    const seen = types(events);
    expect(seen[0]).toBe("iteration_start");
    expect(seen.filter((t) => t === "thinking_delta").length).toBeGreaterThan(1);
    expect(seen.filter((t) => t === "content_delta").length).toBeGreaterThan(1);
    // Live text comes before the complete events, which still carry everything.
    expect(seen.lastIndexOf("content_delta")).toBeLessThan(seen.indexOf("final"));
    const thinking = events.filter((e) => e.type === "thinking_delta").map((e) => (e as { content: string }).content).join("");
    expect(thinking).toBe("Considering. Done thinking. ");
    expect(events.find((e) => e.type === "thinking")).toMatchObject({ content: "Considering. Done thinking. " });
    expect(stub.requests[before].stream).toBe(true);
  });

  it("runs a tool for real and feeds the result back to the model", async () => {
    const events: AgentEvent[] = [];
    const reply = await client.sendMessage({ sessionId: "e2e-tool", message: "please list files" }, (e) => events.push(e));

    expect(types(events)).toEqual(expect.arrayContaining(["tool_call", "tool_result", "final"]));
    expect(events.find((e) => e.type === "tool_call")).toMatchObject({ name: "list_directory" });
    const result = events.find((e) => e.type === "tool_result") as { result: unknown } | undefined;
    expect(JSON.stringify(result?.result)).toContain("hello-e2e.txt");
    // The model's second call carried the tool's output, so its answer names the file.
    expect(reply).toContain("hello-e2e.txt");
    expect(stub.requests.some((r) => r.messages.at(-1)?.role === "tool")).toBe(true);
    expect(stub.requests.every((r) => r.tools > 0)).toBe(true);
  });

  it("lists, restores (with thinking) and deletes sessions", async () => {
    await client.chat({ sessionId: "e2e-history", message: "remember this question" });

    const listed = await client.listSessions();
    expect(listed.find((s) => s.id === "e2e-history")).toMatchObject({ title: "remember this question" });

    const history = await client.getHistory("e2e-history");
    expect(history.map((m) => m.role)).toEqual(["user", "assistant"]);
    expect(history[1].content).toBe("Hello there");
    // What a reopened session shows: the reasoning is saved next to the reply.
    expect(history[1].thinking).toContain("Considering.");

    await client.deleteSession("e2e-history");
    expect(await client.getHistory("e2e-history")).toEqual([]);
    expect((await client.listSessions()).some((s) => s.id === "e2e-history")).toBe(false);
  });

  it("stops a running turn: refuses a second one meanwhile, drops the model request, stays usable", async () => {
    const sessionId = "e2e-stop";
    const events: AgentEvent[] = [];
    const turn = client.chatStream({ sessionId, message: "be slow", streamDeltas: true }, (e) => events.push(e));
    await waitFor(() => events.some((e) => e.type === "thinking_delta"), "the slow turn to start streaming");

    // One turn at a time per session.
    await expect(client.chatStream({ sessionId, message: "again" }, () => {})).rejects.toMatchObject({
      constructor: AtlasApiError,
      message: expect.stringContaining("409"),
    });

    expect(await client.cancel(sessionId)).toBe(true);
    expect(await turn).toBe("");
    expect(events.at(-1)).toEqual({ type: "cancelled" });
    // Atlas hung up on Ollama instead of letting it generate on.
    await Promise.race([stub.clientLeft, sleep(5000).then(() => Promise.reject(new Error("atlas never closed the model connection")))]);

    // Nothing is running any more, and the session carries on.
    expect(await client.cancel(sessionId)).toBe(false);
    expect(await client.sendMessage({ sessionId, message: "hi" }, () => {})).toBe("Hello there");
  });

  it("serves the built UI under /ui/ and redirects / to it", async () => {
    const page = await fetch(`${baseUrl}/ui/`);
    expect(page.status).toBe(200);
    expect(await page.text()).toContain("e2e ui");

    const root = await fetch(`${baseUrl}/`, { redirect: "manual" });
    expect(root.status).toBeGreaterThanOrEqual(300);
    expect(root.headers.get("location")).toBe("/ui/");
  });

  it("allows the browser's cross-origin calls (the UI may run from another origin in development)", async () => {
    const preflight = await fetch(`${baseUrl}/sessions/x`, {
      method: "OPTIONS",
      headers: { Origin: "http://localhost:5173", "Access-Control-Request-Method": "DELETE" },
    });
    expect(preflight.status).toBe(200);
    expect(preflight.headers.get("access-control-allow-origin")).toBe("*");
    expect(preflight.headers.get("access-control-allow-methods")).toContain("DELETE");
  });
});

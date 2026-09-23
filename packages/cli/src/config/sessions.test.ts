import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { loadSessions, recordTurn, sessionsFor } from "./sessions.js";

let dir: string;

beforeEach(() => {
  dir = mkdtempSync(join(tmpdir(), "atlas-cli-sessions-"));
  process.env.ATLAS_CLI_CONFIG_DIR = dir;
});

afterEach(() => {
  delete process.env.ATLAS_CLI_CONFIG_DIR;
  rmSync(dir, { recursive: true, force: true });
});

describe("recordTurn", () => {
  it("creates a new entry on the first turn, labeled from that turn's message", () => {
    recordTurn({ id: "s1", server: "http://a", workspace: "default", message: "fix the crash in list_directory" });
    const [entry] = loadSessions();
    expect(entry).toMatchObject({
      id: "s1",
      server: "http://a",
      workspace: "default",
      label: "fix the crash in list_directory",
      messageCount: 1,
    });
  });

  it("truncates a long first message to 60 characters for the label", () => {
    const long = "x".repeat(200);
    recordTurn({ id: "s1", server: "http://a", workspace: "default", message: long });
    expect(loadSessions()[0]?.label).toHaveLength(60);
  });

  it("bumps messageCount/updatedAt on later turns without overwriting the original label", async () => {
    recordTurn({ id: "s1", server: "http://a", workspace: "default", message: "first message" });
    const firstUpdatedAt = loadSessions()[0]?.updatedAt;

    // updatedAt has second-level (ISO) resolution in practice, so give it
    // a moment to actually differ - otherwise this assertion is
    // meaningless on a fast machine.
    await new Promise((resolve) => setTimeout(resolve, 5));

    recordTurn({ id: "s1", server: "http://a", workspace: "default", message: "second message" });
    const sessions = loadSessions();
    expect(sessions).toHaveLength(1);
    expect(sessions[0]).toMatchObject({ label: "first message", messageCount: 2 });
    expect(sessions[0]?.updatedAt).not.toBe(firstUpdatedAt);
  });
});

describe("sessionsFor", () => {
  it("only returns sessions matching both server and workspace", () => {
    recordTurn({ id: "s1", server: "http://a", workspace: "default", message: "a/default" });
    recordTurn({ id: "s2", server: "http://a", workspace: "other", message: "a/other" });
    recordTurn({ id: "s3", server: "http://b", workspace: "default", message: "b/default" });

    const matches = sessionsFor("http://a", "default");
    expect(matches.map((s) => s.id)).toEqual(["s1"]);
  });

  it("sorts most recently used first", async () => {
    recordTurn({ id: "older", server: "http://a", workspace: "default", message: "older" });
    await new Promise((resolve) => setTimeout(resolve, 5));
    recordTurn({ id: "newer", server: "http://a", workspace: "default", message: "newer" });

    const matches = sessionsFor("http://a", "default");
    expect(matches.map((s) => s.id)).toEqual(["newer", "older"]);
  });
});

describe("loadSessions", () => {
  it("returns [] when no registry file exists yet", () => {
    expect(loadSessions()).toEqual([]);
  });
});

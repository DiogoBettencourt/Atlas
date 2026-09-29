// Covers the tab-switching shell itself, not the Chat/Sessions content
// each tab renders (App.test.tsx and SessionPicker.test.tsx already
// cover those directly). Uses a real recordTurn()/sessionsFor() round
// trip against a temp config dir, same convention as
// config/sessions.test.ts, rather than mocking the session registry.
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { render } from "ink-testing-library";
import React from "react";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import type { AtlasClient } from "../api/client.js";
import { recordTurn } from "../config/sessions.js";
import Root from "./Root.js";

function wait(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

// Root only calls client.health() (via the App it mounts for the Chat
// tab) - a real AtlasClient isn't needed for shell-level behavior.
const fakeClient = { health: async () => true } as unknown as AtlasClient;

let dir: string;

beforeEach(() => {
  dir = mkdtempSync(join(tmpdir(), "atlas-cli-root-"));
  process.env.ATLAS_CLI_CONFIG_DIR = dir;
});

afterEach(() => {
  delete process.env.ATLAS_CLI_CONFIG_DIR;
  rmSync(dir, { recursive: true, force: true });
});

describe("Root", () => {
  it("starts on the Chat tab by default, showing the App banner and neither session list", async () => {
    recordTurn({ id: "s1", server: "http://a", workspace: "default", message: "fix the crash" });
    const { lastFrame, unmount } = render(
      React.createElement(Root, {
        client: fakeClient,
        initialSessionId: "current",
        initialTab: "chat",
        server: "http://a",
        workspace: "default",
      })
    );
    await wait(30);
    const frame = lastFrame() ?? "";
    expect(frame).toContain("ATLAS");
    expect(frame).not.toContain("fix the crash");
    unmount();
  });

  it("starts on the Sessions tab when asked, listing existing sessions instead of the Chat banner", async () => {
    recordTurn({ id: "s1", server: "http://a", workspace: "default", message: "fix the crash" });
    const { lastFrame, unmount } = render(
      React.createElement(Root, {
        client: fakeClient,
        initialSessionId: "current",
        initialTab: "sessions",
        server: "http://a",
        workspace: "default",
      })
    );
    await wait(30);
    const frame = lastFrame() ?? "";
    expect(frame).toContain("fix the crash");
    expect(frame).not.toContain("ATLAS");
    unmount();
  });

  it("pressing Tab switches from Chat to Sessions and back", async () => {
    recordTurn({ id: "s1", server: "http://a", workspace: "default", message: "fix the crash" });
    const { stdin, lastFrame, unmount } = render(
      React.createElement(Root, {
        client: fakeClient,
        initialSessionId: "current",
        initialTab: "chat",
        server: "http://a",
        workspace: "default",
      })
    );
    await wait(30);
    expect(lastFrame() ?? "").toContain("ATLAS");

    stdin.write("\t");
    await wait(30);
    expect(lastFrame() ?? "").toContain("fix the crash");

    stdin.write("\t");
    await wait(30);
    expect(lastFrame() ?? "").toContain("ATLAS");

    unmount();
  });

  it("selecting a session on the Sessions tab switches to Chat with that session's id", async () => {
    recordTurn({ id: "s1", server: "http://a", workspace: "default", message: "fix the crash" });
    const { stdin, lastFrame, unmount } = render(
      React.createElement(Root, {
        client: fakeClient,
        initialSessionId: "current",
        initialTab: "sessions",
        server: "http://a",
        workspace: "default",
      })
    );
    await wait(30);

    // Row 0 is "start a new session"; one down-arrow selects s1.
    stdin.write("\u001B[B");
    await wait(20);
    stdin.write("\r");
    await wait(30);

    const frame = lastFrame() ?? "";
    expect(frame).toContain("ATLAS"); // switched back to Chat
    expect(frame).toContain("Session: s1");

    unmount();
  });
});

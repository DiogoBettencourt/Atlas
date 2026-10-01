import { render } from "ink-testing-library";
import React from "react";
import { describe, expect, it } from "vitest";
import type { SessionRecord } from "../config/sessions.js";
import SessionPicker from "./SessionPicker.js";

function wait(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

const SESSIONS: SessionRecord[] = [
  { id: "s1", server: "http://a", workspace: "default", label: "fix the crash", messageCount: 3, updatedAt: new Date().toISOString() },
  { id: "s2", server: "http://a", workspace: "default", label: "scaffold the cli", messageCount: 7, updatedAt: new Date().toISOString() },
];

describe("SessionPicker", () => {
  it("renders 'start a new session' plus every given session's label", async () => {
    const { lastFrame, unmount } = render(
      React.createElement(SessionPicker, { sessions: SESSIONS, onSelect: () => {}, onDelete: () => {} })
    );
    await wait(20);
    const frame = lastFrame() ?? "";
    expect(frame).toContain("start a new session");
    expect(frame).toContain("fix the crash");
    expect(frame).toContain("scaffold the cli");
    unmount();
  });

  it("selects 'start a new session' (undefined) on Enter with no navigation", async () => {
    let selected: string | undefined | "not-called" = "not-called";
    const { stdin, unmount } = render(
      React.createElement(SessionPicker, {
        sessions: SESSIONS,
        onSelect: (id) => {
          selected = id;
        },
        onDelete: () => {},
      })
    );
    await wait(20);
    stdin.write("\r");
    await wait(20);
    expect(selected).toBeUndefined();
    unmount();
  });

  it("moving down twice then Enter selects the second session", async () => {
    let selected: string | undefined | "not-called" = "not-called";
    const { stdin, unmount } = render(
      React.createElement(SessionPicker, {
        sessions: SESSIONS,
        onSelect: (id) => {
          selected = id;
        },
        onDelete: () => {},
      })
    );
    await wait(20);
    stdin.write("\u001B[B"); // down arrow -> row 1 (s1)
    await wait(10);
    stdin.write("\u001B[B"); // down arrow -> row 2 (s2)
    await wait(10);
    stdin.write("\r");
    await wait(20);
    expect(selected).toBe("s2");
    unmount();
  });

  it("wraps from the last row back to 'start a new session'", async () => {
    let selected: string | undefined | "not-called" = "not-called";
    const { stdin, unmount } = render(
      React.createElement(SessionPicker, {
        sessions: SESSIONS,
        onSelect: (id) => {
          selected = id;
        },
        onDelete: () => {},
      })
    );
    await wait(20);
    // 3 rows total (new + 2 sessions); 3 down-presses from row 0 wraps
    // back to row 0.
    for (let i = 0; i < 3; i++) {
      stdin.write("\u001B[B");
      await wait(10);
    }
    stdin.write("\r");
    await wait(20);
    expect(selected).toBeUndefined();
    unmount();
  });

  it("asks for confirmation on 'd' instead of deleting immediately", async () => {
    let deleted: string | "not-called" = "not-called";
    const { stdin, lastFrame, unmount } = render(
      React.createElement(SessionPicker, {
        sessions: SESSIONS,
        onSelect: () => {},
        onDelete: (id) => {
          deleted = id;
        },
      })
    );
    await wait(20);
    stdin.write("\u001B[B"); // down arrow -> row 1 (s1)
    await wait(10);
    stdin.write("d");
    await wait(20);
    expect(lastFrame() ?? "").toContain("Delete");
    expect(deleted).toBe("not-called"); // not yet - still waiting on y/n
    unmount();
  });

  it("deletes on 'd' then 'y'", async () => {
    let deleted: string | "not-called" = "not-called";
    const { stdin, unmount } = render(
      React.createElement(SessionPicker, {
        sessions: SESSIONS,
        onSelect: () => {},
        onDelete: (id) => {
          deleted = id;
        },
      })
    );
    await wait(20);
    stdin.write("\u001B[B"); // down arrow -> row 1 (s1)
    await wait(10);
    stdin.write("d");
    await wait(10);
    stdin.write("y");
    await wait(20);
    expect(deleted).toBe("s1");
    unmount();
  });

  it("cancels on 'd' then 'n', leaving the session alone", async () => {
    let deleted: string | "not-called" = "not-called";
    const { stdin, unmount } = render(
      React.createElement(SessionPicker, {
        sessions: SESSIONS,
        onSelect: () => {},
        onDelete: (id) => {
          deleted = id;
        },
      })
    );
    await wait(20);
    stdin.write("\u001B[B"); // down arrow -> row 1 (s1)
    await wait(10);
    stdin.write("d");
    await wait(10);
    stdin.write("n");
    await wait(20);
    expect(deleted).toBe("not-called");
    unmount();
  });

  it("'d' on the 'start a new session' row does nothing", async () => {
    let deleted: string | "not-called" = "not-called";
    const { stdin, unmount } = render(
      React.createElement(SessionPicker, {
        sessions: SESSIONS,
        onSelect: () => {},
        onDelete: (id) => {
          deleted = id;
        },
      })
    );
    await wait(20);
    stdin.write("d"); // still on row 0
    await wait(20);
    expect(deleted).toBe("not-called");
    unmount();
  });
});

import { describe, expect, it } from "vitest";
import { applyEvent, failTurn, newAssistantTurn, turnsFromHistory } from "./chatState";

describe("applyEvent", () => {
  it("records iteration progress", () => {
    const t = applyEvent(newAssistantTurn("a"), { type: "iteration_start", iteration: 2, max_iterations: 20 });
    expect(t.iteration).toBe(2);
    expect(t.maxIterations).toBe(20);
    expect(t.blocks).toEqual([]);
  });

  it("appends thinking, thought, tool and final blocks in order, with unique ids", () => {
    let t = newAssistantTurn("a");
    t = applyEvent(t, { type: "iteration_start", iteration: 1, max_iterations: 20 });
    t = applyEvent(t, { type: "thinking", content: "hmm" });
    t = applyEvent(t, { type: "assistant_thought", content: "I'll read it." });
    t = applyEvent(t, { type: "tool_call", name: "read_file", arguments: { path: "x" } });
    t = applyEvent(t, { type: "tool_result", name: "read_file", result: { content: "hi" } });
    t = applyEvent(t, { type: "final", reply: "done" });

    expect(t.blocks.map((b) => b.kind)).toEqual(["thinking", "thought", "tool", "text"]);
    expect(new Set(t.blocks.map((b) => b.id)).size).toBe(4);
    expect(t.blocks[0]).toMatchObject({ kind: "thinking", iteration: 1, text: "hmm" });
    expect(t.blocks[2]).toMatchObject({ kind: "tool", name: "read_file", done: true, result: { content: "hi" } });
    expect(t.status).toBe("done");
  });

  it("pairs each result with the oldest unfinished call of the same tool", () => {
    let t = newAssistantTurn("a");
    t = applyEvent(t, { type: "tool_call", name: "read_file", arguments: { path: "1" } });
    t = applyEvent(t, { type: "tool_call", name: "read_file", arguments: { path: "2" } });
    t = applyEvent(t, { type: "tool_result", name: "read_file", result: "first" });
    expect(t.blocks.map((b) => b.kind === "tool" && [b.args, b.done, b.result])).toEqual([
      [{ path: "1" }, true, "first"],
      [{ path: "2" }, false, undefined],
    ]);
  });

  it("shows an orphan result as a finished tool block instead of dropping it", () => {
    const t = applyEvent(newAssistantTurn("a"), { type: "tool_result", name: "git", result: "ok" });
    expect(t.blocks).toHaveLength(1);
    expect(t.blocks[0]).toMatchObject({ kind: "tool", name: "git", done: true });
  });

  it("does not mutate the previous turn", () => {
    const before = newAssistantTurn("a");
    applyEvent(before, { type: "thinking", content: "x" });
    expect(before.blocks).toEqual([]);
  });

  it("marks the turn failed on an error event or failTurn", () => {
    const a = applyEvent(newAssistantTurn("a"), { type: "error", message: "boom" });
    expect(a).toMatchObject({ status: "error", error: "boom" });
    expect(failTurn(newAssistantTurn("b"), "gone")).toMatchObject({ status: "error", error: "gone" });
  });
});

describe("turnsFromHistory", () => {
  it("rebuilds user turns, tool calls with results, and the final answer", () => {
    const turns = turnsFromHistory([
      { role: "user", content: "read hello.txt" },
      {
        role: "assistant",
        content: "Let me look.",
        tool_calls: [{ function: { name: "read_file", arguments: { path: "hello.txt" } } }],
      },
      { role: "tool", name: "read_file", content: '{"content":"hello"}' },
      { role: "assistant", content: "It says hello." },
      { role: "user", content: "thanks" },
      { role: "assistant", content: "Anytime." },
    ]);

    expect(turns.map((t) => t.role)).toEqual(["user", "assistant", "user", "assistant"]);
    const first = turns[1];
    if (first.role !== "assistant") throw new Error("expected assistant");
    expect(first.status).toBe("done");
    expect(first.blocks.map((b) => b.kind)).toEqual(["thought", "tool", "text"]);
    expect(first.blocks[1]).toMatchObject({
      name: "read_file",
      args: { path: "hello.txt" },
      result: { content: "hello" },
      done: true,
    });
  });

  it("parses string tool arguments (OpenAI style) and keeps non-JSON results as text", () => {
    const turns = turnsFromHistory([
      { role: "user", content: "go" },
      { role: "assistant", content: "", tool_calls: [{ function: { name: "t", arguments: '{"a":1}' } }] },
      { role: "tool", name: "t", content: "plain output" },
    ]);
    const a = turns[1];
    if (a.role !== "assistant") throw new Error("expected assistant");
    expect(a.blocks).toHaveLength(1);
    expect(a.blocks[0]).toMatchObject({ args: { a: 1 }, result: "plain output", done: true });
  });

  it("shows a tool call with no recorded result as finished, not running forever", () => {
    const turns = turnsFromHistory([
      { role: "user", content: "go" },
      { role: "assistant", content: "", tool_calls: [{ function: { name: "t", arguments: {} } }] },
    ]);
    const a = turns[1];
    if (a.role !== "assistant") throw new Error("expected assistant");
    expect(a.blocks[0]).toMatchObject({ kind: "tool", done: true });
  });

  it("returns nothing for an empty history and ignores assistant turns with no content", () => {
    expect(turnsFromHistory([])).toEqual([]);
    expect(turnsFromHistory([{ role: "user", content: "x" }, { role: "assistant", content: "" }])).toEqual([
      { id: "h-u0", role: "user", text: "x" },
    ]);
  });
});

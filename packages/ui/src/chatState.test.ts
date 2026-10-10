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

describe("applyEvent with live deltas", () => {
  it("grows one thinking block from thinking_delta events, then replaces it with the complete text", () => {
    let t = applyEvent(newAssistantTurn("a"), { type: "iteration_start", iteration: 1, max_iterations: 20 });
    t = applyEvent(t, { type: "thinking_delta", content: "The user " });
    t = applyEvent(t, { type: "thinking_delta", content: "wants X." });
    expect(t.blocks).toHaveLength(1);
    expect(t.blocks[0]).toMatchObject({ kind: "thinking", iteration: 1, text: "The user wants X.", streaming: true });

    const liveId = t.blocks[0].id;
    t = applyEvent(t, { type: "thinking", content: "The user wants X." });
    expect(t.blocks).toHaveLength(1);
    expect(t.blocks[0]).toMatchObject({ kind: "thinking", text: "The user wants X." });
    expect((t.blocks[0] as { streaming?: boolean }).streaming).toBeFalsy();
    expect(t.blocks[0].id).toBe(liveId);
  });

  it("grows the reply from content_delta events and settles it on final", () => {
    let t = newAssistantTurn("a");
    t = applyEvent(t, { type: "thinking_delta", content: "hmm" });
    t = applyEvent(t, { type: "content_delta", content: "Hel" });
    t = applyEvent(t, { type: "content_delta", content: "lo" });
    expect(t.blocks.map((b) => b.kind)).toEqual(["thinking", "text"]);
    expect(t.blocks[1]).toMatchObject({ text: "Hello", streaming: true });

    // The full "thinking" event arrives after the content deltas and must still find its block.
    t = applyEvent(t, { type: "thinking", content: "hmm" });
    expect(t.blocks.map((b) => b.kind)).toEqual(["thinking", "text"]);

    t = applyEvent(t, { type: "final", reply: "Hello" });
    expect(t.blocks).toHaveLength(2);
    expect(t.blocks[1]).toMatchObject({ kind: "text", text: "Hello" });
    expect((t.blocks[1] as { streaming?: boolean }).streaming).toBeFalsy();
    expect(t.status).toBe("done");
  });

  it("turns streamed reply text into a thought when tool calls follow", () => {
    let t = newAssistantTurn("a");
    t = applyEvent(t, { type: "content_delta", content: "Let me look." });
    t = applyEvent(t, { type: "assistant_thought", content: "Let me look." });
    t = applyEvent(t, { type: "tool_call", name: "read_file", arguments: { path: "x" } });
    expect(t.blocks.map((b) => b.kind)).toEqual(["thought", "tool"]);
    expect(t.blocks[0]).toMatchObject({ text: "Let me look." });
  });

  it("starts a fresh block for the next iteration's thinking", () => {
    let t = newAssistantTurn("a");
    t = applyEvent(t, { type: "thinking_delta", content: "one" });
    t = applyEvent(t, { type: "thinking", content: "one" });
    t = applyEvent(t, { type: "tool_call", name: "t", arguments: {} });
    t = applyEvent(t, { type: "tool_result", name: "t", result: "r" });
    t = applyEvent(t, { type: "thinking_delta", content: "two" });
    expect(t.blocks.filter((b) => b.kind === "thinking").map((b) => (b as { text: string }).text)).toEqual(["one", "two"]);
  });

  it("stops marking text as live when the turn fails or errors", () => {
    const live = applyEvent(newAssistantTurn("a"), { type: "content_delta", content: "par" });
    for (const t of [failTurn(live, "gone"), applyEvent(live, { type: "error", message: "boom" })]) {
      expect(t.status).toBe("error");
      expect((t.blocks[0] as { streaming?: boolean }).streaming).toBeFalsy();
      expect(t.blocks[0]).toMatchObject({ text: "par" });
    }
  });

  it("gives the same result with or without deltas", () => {
    const withDeltas = ["thinking_delta", "content_delta"].reduce((turn, type) => {
      return applyEvent(turn, { type, content: "x" } as never);
    }, newAssistantTurn("a"));
    expect(withDeltas.blocks).toHaveLength(2);

    const full = (deltas: boolean) => {
      let t = newAssistantTurn("a");
      if (deltas) {
        t = applyEvent(t, { type: "thinking_delta", content: "why" });
        t = applyEvent(t, { type: "content_delta", content: "answer" });
      }
      t = applyEvent(t, { type: "thinking", content: "why" });
      t = applyEvent(t, { type: "final", reply: "answer" });
      return t.blocks.map((b) => ({ ...b, id: "" }));
    };
    expect(full(true)).toEqual(full(false));
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

describe("stopping a turn", () => {
  it("a cancelled event ends the turn, keeps what was said, and stops the live markers", () => {
    let t = newAssistantTurn("a");
    t = applyEvent(t, { type: "thinking_delta", content: "Let me " });
    t = applyEvent(t, { type: "thinking_delta", content: "think" });
    t = applyEvent(t, { type: "content_delta", content: "Part of an answ" });
    t = applyEvent(t, { type: "cancelled" });

    expect(t.status).toBe("cancelled");
    expect(t.blocks).toMatchObject([
      { kind: "thinking", text: "Let me think", streaming: false },
      { kind: "text", text: "Part of an answ", streaming: false },
    ]);
  });

  it("leaves a tool that was still running unfinished (the transcript shows it as stopped)", () => {
    let t = newAssistantTurn("a");
    t = applyEvent(t, { type: "tool_call", name: "read_file", arguments: {} });
    t = applyEvent(t, { type: "cancelled" });
    expect(t.status).toBe("cancelled");
    expect(t.blocks[0]).toMatchObject({ kind: "tool", done: false });
  });
});

describe("turnsFromHistory thinking", () => {
  it("restores each reply's saved reasoning ahead of what it said, numbered per iteration", () => {
    const turns = turnsFromHistory([
      { role: "user", content: "read it" },
      { role: "assistant", content: "", thinking: "I should read the file.", tool_calls: [{ function: { name: "read_file", arguments: {} } }] },
      { role: "tool", name: "read_file", content: "{}" },
      { role: "assistant", content: "Done.", thinking: "Now answer." },
    ]);
    const assistant = turns[1];
    if (assistant.role !== "assistant") throw new Error("expected an assistant turn");
    expect(assistant.blocks.map((b) => b.kind)).toEqual(["thinking", "tool", "thinking", "text"]);
    expect(assistant.blocks[0]).toMatchObject({ iteration: 1, text: "I should read the file." });
    expect(assistant.blocks[2]).toMatchObject({ iteration: 2, text: "Now answer." });
    expect(assistant.blocks[0]).not.toHaveProperty("streaming", true);
  });

  it("ignores empty or whitespace-only thinking", () => {
    const turns = turnsFromHistory([
      { role: "user", content: "hi" },
      { role: "assistant", content: "Hello", thinking: "  \n" },
    ]);
    const assistant = turns[1];
    if (assistant.role !== "assistant") throw new Error("expected an assistant turn");
    expect(assistant.blocks.map((b) => b.kind)).toEqual(["text"]);
  });
});

// Pure chat-transcript model: the shapes the UI renders, how a /chat/stream
// event folds into an assistant turn, and how a persisted session history
// becomes turns again. No React in here so it is easy to test.
import type { AgentEvent, RawSessionMessage } from "@atlas/client";

export type Block =
  | { kind: "thinking"; id: string; iteration: number; text: string }
  | { kind: "thought"; id: string; text: string }
  | { kind: "tool"; id: string; name: string; args: unknown; result?: unknown; done: boolean }
  | { kind: "text"; id: string; text: string };

export interface UserTurn {
  id: string;
  role: "user";
  text: string;
}

export interface AssistantTurn {
  id: string;
  role: "assistant";
  blocks: Block[];
  status: "running" | "done" | "error";
  error?: string;
  iteration: number;
  maxIterations: number;
}

export type Turn = UserTurn | AssistantTurn;

export function newAssistantTurn(id: string): AssistantTurn {
  return { id, role: "assistant", blocks: [], status: "running", iteration: 0, maxIterations: 0 };
}

// Folds one stream event into an assistant turn, returning a new turn.
export function applyEvent(turn: AssistantTurn, event: AgentEvent): AssistantTurn {
  const nextId = `${turn.id}-${turn.blocks.length}`;
  switch (event.type) {
    case "iteration_start":
      return { ...turn, iteration: event.iteration, maxIterations: event.max_iterations };
    case "thinking":
      return {
        ...turn,
        blocks: [...turn.blocks, { kind: "thinking", id: nextId, iteration: turn.iteration, text: event.content }],
      };
    case "assistant_thought":
      return { ...turn, blocks: [...turn.blocks, { kind: "thought", id: nextId, text: event.content }] };
    case "tool_call":
      return {
        ...turn,
        blocks: [...turn.blocks, { kind: "tool", id: nextId, name: event.name, args: event.arguments, done: false }],
      };
    case "tool_result": {
      // Pair the result with the oldest still-running call of that tool.
      const index = turn.blocks.findIndex((b) => b.kind === "tool" && b.name === event.name && !b.done);
      if (index < 0) {
        return {
          ...turn,
          blocks: [
            ...turn.blocks,
            { kind: "tool", id: nextId, name: event.name, args: undefined, result: event.result, done: true },
          ],
        };
      }
      const blocks = turn.blocks.slice();
      const call = blocks[index] as Extract<Block, { kind: "tool" }>;
      blocks[index] = { ...call, result: event.result, done: true };
      return { ...turn, blocks };
    }
    case "final":
      return { ...turn, blocks: [...turn.blocks, { kind: "text", id: nextId, text: event.reply }], status: "done" };
    case "error":
      return { ...turn, status: "error", error: event.message };
  }
}

// Marks a turn as failed with a client-side error (the stream died, the
// server was unreachable) and stops any tool still shown as running.
export function failTurn(turn: AssistantTurn, message: string): AssistantTurn {
  return { ...turn, status: "error", error: message };
}

interface RawToolCall {
  function?: { name?: unknown; arguments?: unknown };
}

function parseArguments(value: unknown): unknown {
  if (typeof value !== "string") return value;
  try {
    return JSON.parse(value);
  } catch {
    return value;
  }
}

function parseResult(value: unknown): unknown {
  if (typeof value !== "string") return value;
  try {
    return JSON.parse(value);
  } catch {
    return value;
  }
}

// Rebuilds the transcript from GET /sessions/:id/history. Unlike AtlasCLI,
// which only shows the user/assistant text, this also restores the tool
// calls (and their results) between a question and its answer, so a
// reopened session looks like the live one. Thinking is not persisted, so
// it can't be restored.
export function turnsFromHistory(messages: RawSessionMessage[], idPrefix = "h"): Turn[] {
  const turns: Turn[] = [];
  let current: AssistantTurn | undefined;

  const ensureAssistant = (): AssistantTurn => {
    if (!current) {
      current = { ...newAssistantTurn(`${idPrefix}-a${turns.length}`), status: "done" };
      turns.push(current);
    }
    return current;
  };

  for (const m of messages) {
    if (m.role === "user") {
      current = undefined;
      if (typeof m.content === "string") {
        turns.push({ id: `${idPrefix}-u${turns.length}`, role: "user", text: m.content });
      }
    } else if (m.role === "assistant") {
      const turn = ensureAssistant();
      if (typeof m.content === "string" && m.content.length > 0) {
        const hasCalls = Array.isArray(m.tool_calls) && m.tool_calls.length > 0;
        turn.blocks.push({
          kind: hasCalls ? "thought" : "text",
          id: `${turn.id}-${turn.blocks.length}`,
          text: m.content,
        } as Block);
      }
      for (const call of (m.tool_calls ?? []) as RawToolCall[]) {
        turn.blocks.push({
          kind: "tool",
          id: `${turn.id}-${turn.blocks.length}`,
          name: typeof call.function?.name === "string" ? call.function.name : "tool",
          args: parseArguments(call.function?.arguments),
          done: false,
        });
      }
    } else if (m.role === "tool") {
      const turn = ensureAssistant();
      const index = turn.blocks.findIndex((b) => b.kind === "tool" && !b.done && (!m.name || b.name === m.name));
      const result = parseResult(m.content);
      if (index >= 0) {
        const call = turn.blocks[index] as Extract<Block, { kind: "tool" }>;
        turn.blocks[index] = { ...call, result, done: true };
      } else {
        turn.blocks.push({
          kind: "tool",
          id: `${turn.id}-${turn.blocks.length}`,
          name: m.name ?? "tool",
          args: undefined,
          result,
          done: true,
        });
      }
    }
  }

  // A tool call whose result never got persisted (the run was cut off) is
  // shown as finished rather than spinning forever.
  for (const turn of turns) {
    if (turn.role !== "assistant") continue;
    turn.blocks = turn.blocks.map((b) => (b.kind === "tool" && !b.done ? { ...b, done: true } : b));
  }
  return turns.filter((t) => t.role === "user" || t.blocks.length > 0);
}

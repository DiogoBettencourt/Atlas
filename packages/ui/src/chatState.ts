// Pure chat-transcript model: the shapes the UI renders, how a /chat/stream
// event folds into an assistant turn, and how a persisted session history
// becomes turns again. No React in here so it is easy to test.
import type { AgentEvent, RawSessionMessage } from "@atlas/client";

// `streaming` marks text still being generated (it grows with each delta
// event); the complete event that follows replaces it and clears the flag.
export type Block =
  | { kind: "thinking"; id: string; iteration: number; text: string; streaming?: boolean }
  | { kind: "thought"; id: string; text: string }
  | { kind: "tool"; id: string; name: string; args: unknown; result?: unknown; done: boolean }
  | { kind: "text"; id: string; text: string; streaming?: boolean };

export interface UserTurn {
  id: string;
  role: "user";
  text: string;
}

export interface AssistantTurn {
  id: string;
  role: "assistant";
  blocks: Block[];
  status: "running" | "done" | "error" | "cancelled";
  error?: string;
  iteration: number;
  maxIterations: number;
}

export type Turn = UserTurn | AssistantTurn;

export function newAssistantTurn(id: string): AssistantTurn {
  return { id, role: "assistant", blocks: [], status: "running", iteration: 0, maxIterations: 0 };
}

function lastIndexWhere(blocks: Block[], test: (b: Block) => boolean): number {
  for (let i = blocks.length - 1; i >= 0; i--) {
    if (test(blocks[i])) return i;
  }
  return -1;
}

const isLiveThinking = (b: Block) => b.kind === "thinking" && b.streaming === true;
const isLiveText = (b: Block) => b.kind === "text" && b.streaming === true;

// Replaces the block at `index` (or appends if there is none).
function withBlock(turn: AssistantTurn, index: number, block: Block): AssistantTurn {
  if (index < 0) return { ...turn, blocks: [...turn.blocks, block] };
  const blocks = turn.blocks.slice();
  blocks[index] = block;
  return { ...turn, blocks };
}

// A turn that ended (final, error) shouldn't keep blocks marked as live.
function settle(turn: AssistantTurn): AssistantTurn {
  if (!turn.blocks.some((b) => (b.kind === "thinking" || b.kind === "text") && b.streaming)) return turn;
  return {
    ...turn,
    blocks: turn.blocks.map((b) => ((b.kind === "thinking" || b.kind === "text") && b.streaming ? { ...b, streaming: false } : b)),
  };
}

// Folds one stream event into an assistant turn, returning a new turn.
//
// Live text arrives as thinking_delta / content_delta (just the new piece)
// and is appended to a growing block. The complete event for the same text
// (thinking / assistant_thought / final) then replaces that block, so the
// result is identical whether or not the deltas were sent.
export function applyEvent(turn: AssistantTurn, event: AgentEvent): AssistantTurn {
  const nextId = `${turn.id}-${turn.blocks.length}`;
  switch (event.type) {
    case "iteration_start":
      return { ...turn, iteration: event.iteration, maxIterations: event.max_iterations };
    case "thinking_delta": {
      const index = lastIndexWhere(turn.blocks, isLiveThinking);
      const current = index >= 0 ? (turn.blocks[index] as Extract<Block, { kind: "thinking" }>) : undefined;
      return withBlock(turn, index, {
        kind: "thinking",
        id: current?.id ?? nextId,
        iteration: current?.iteration ?? turn.iteration,
        text: (current?.text ?? "") + event.content,
        streaming: true,
      });
    }
    case "content_delta": {
      const index = lastIndexWhere(turn.blocks, isLiveText);
      const current = index >= 0 ? (turn.blocks[index] as Extract<Block, { kind: "text" }>) : undefined;
      return withBlock(turn, index, {
        kind: "text",
        id: current?.id ?? nextId,
        text: (current?.text ?? "") + event.content,
        streaming: true,
      });
    }
    case "thinking": {
      const index = lastIndexWhere(turn.blocks, isLiveThinking);
      const current = index >= 0 ? (turn.blocks[index] as Extract<Block, { kind: "thinking" }>) : undefined;
      return withBlock(turn, index, {
        kind: "thinking",
        id: current?.id ?? nextId,
        iteration: current?.iteration ?? turn.iteration,
        text: event.content,
      });
    }
    case "assistant_thought": {
      // The reply text that was streaming turns out to be a remark made
      // alongside tool calls, not the final answer.
      const index = lastIndexWhere(turn.blocks, isLiveText);
      const id = index >= 0 ? turn.blocks[index].id : nextId;
      return withBlock(turn, index, { kind: "thought", id, text: event.content });
    }
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
    case "final": {
      const index = lastIndexWhere(turn.blocks, isLiveText);
      const id = index >= 0 ? turn.blocks[index].id : nextId;
      return settle({ ...withBlock(turn, index, { kind: "text", id, text: event.reply }), status: "done" });
    }
    case "cancelled":
      return settle({ ...turn, status: "cancelled" });
    case "error":
      return settle({ ...turn, status: "error", error: event.message });
  }
}

// Marks a turn as failed with a client-side error (the stream died, the
// server was unreachable) and stops any tool still shown as running.
export function failTurn(turn: AssistantTurn, message: string): AssistantTurn {
  return settle({ ...turn, status: "error", error: message });
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
// reopened session looks like the live one, thinking included (Atlas saves
// a reply's reasoning next to it).
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
      if (typeof m.thinking === "string" && m.thinking.trim().length > 0) {
        const iteration = turn.blocks.filter((b) => b.kind === "thinking").length + 1;
        turn.blocks.push({ kind: "thinking", id: `${turn.id}-${turn.blocks.length}`, iteration, text: m.thinking });
      }
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

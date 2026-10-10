import { useEffect, useRef } from "react";
import type { AssistantTurn, Block, Turn } from "./chatState";
import { formatArgs, formatResult } from "./format";
import { RichText } from "./RichText";

function Spinner() {
  return (
    <svg className="spin" width="14" height="14" viewBox="0 0 14 14" fill="none" aria-hidden="true">
      <circle cx="7" cy="7" r="5" stroke="currentColor" strokeWidth="1.6" opacity="0.25" />
      <path d="M7 2a5 5 0 0 1 5 5" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" />
    </svg>
  );
}

function ToolCard({ block }: { block: Extract<Block, { kind: "tool" }> }) {
  const args = formatArgs(block.args);
  return (
    <div className={`tool${block.done ? "" : " tool-running"}`}>
      <div className="tool-head">
        <svg width="16" height="16" viewBox="0 0 16 16" fill="none" aria-hidden="true">
          <path
            d="M9.5 2.5a3 3 0 0 0-3.2 4L2.5 10.3a1.4 1.4 0 0 0 2 2L8.3 8.5a3 3 0 0 0 4-3.2L10.5 7 9 5.5l1.5-1.8z"
            stroke="var(--accent)"
            strokeWidth="1.3"
            strokeLinejoin="round"
          />
        </svg>
        <span className="tool-name">{block.name}</span>
        <span className="tool-args">{args}</span>
        {block.done ? (
          <span className="tool-status tool-done">
            <svg width="14" height="14" viewBox="0 0 14 14" fill="none" aria-hidden="true">
              <path d="M3 7.5l2.5 2.5L11 4.5" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" />
            </svg>
            Done
          </span>
        ) : (
          <span className="tool-status tool-active">
            <Spinner />
            Running
          </span>
        )}
      </div>
      {block.done && block.result !== undefined && (
        <details className="tool-result">
          <summary>Result</summary>
          <pre>{formatResult(block.result)}</pre>
        </details>
      )}
    </div>
  );
}

// The reasoning text. While it streams it follows its own tail, like the
// page does, so the newest words are always the ones in view.
function ThinkingText({ text, live }: { text: string; live: boolean }) {
  const ref = useRef<HTMLParagraphElement>(null);
  useEffect(() => {
    const el = ref.current;
    if (live && el) el.scrollTop = el.scrollHeight;
  }, [text, live]);
  return (
    <p ref={ref}>
      {text}
      {live && <span className="caret" aria-hidden="true" />}
    </p>
  );
}

function BlockView({ block, running }: { block: Block; running: boolean }) {
  switch (block.kind) {
    case "thinking":
      return (
        <details className="thinking" open={running || block.streaming === true}>
          <summary>
            <span className={`dot${block.streaming ? " dot-live" : ""}`} />
            Thinking{block.iteration > 0 ? ` · iteration ${block.iteration}` : ""}
          </summary>
          <ThinkingText text={block.text} live={block.streaming === true} />
        </details>
      );
    case "thought":
      return <RichText text={block.text} />;
    case "tool":
      return <ToolCard block={block} />;
    case "text":
      return (
        <div>
          <RichText text={block.text} />
          {block.streaming && <span className="caret" aria-hidden="true" />}
        </div>
      );
  }
}

function AssistantView({ turn }: { turn: AssistantTurn }) {
  const running = turn.status === "running";
  const lastBlock = turn.blocks[turn.blocks.length - 1];
  // Between events the agent is busy (the model is generating); show that
  // rather than a frozen-looking screen.
  const live = (lastBlock?.kind === "thinking" || lastBlock?.kind === "text") && lastBlock.streaming === true;
  // Nothing is moving on screen: a tool is running (it has its own spinner),
  // or text is already streaming in.
  const waiting = running && !live && !(lastBlock?.kind === "tool" && !lastBlock.done);
  return (
    <div className="assistant" data-testid="assistant-turn">
      {turn.blocks.map((block, i) => (
        <BlockView
          key={block.id}
          block={block}
          // Only the newest thinking block stays open while the turn runs.
          running={running && i === turn.blocks.length - 1}
        />
      ))}
      {waiting && (
        <div className="working" role="status">
          <Spinner />
          {turn.iteration > 0 ? `Working · step ${turn.iteration} of ${turn.maxIterations}` : "Working…"}
        </div>
      )}
      {turn.status === "error" && (
        <div className="error" role="alert">
          {turn.error ?? "Something went wrong."}
        </div>
      )}
    </div>
  );
}

export function Transcript({ turns }: { turns: Turn[] }) {
  const endRef = useRef<HTMLDivElement>(null);
  const scrollRef = useRef<HTMLDivElement>(null);
  const stickRef = useRef(true);

  // Follow the stream, but only while the reader is already at the bottom;
  // scrolling up to re-read something must not get yanked back down.
  useEffect(() => {
    if (stickRef.current) endRef.current?.scrollIntoView?.({ block: "end" });
  }, [turns]);

  return (
    <div
      className="transcript"
      ref={scrollRef}
      onScroll={() => {
        const el = scrollRef.current;
        if (el) stickRef.current = el.scrollHeight - el.scrollTop - el.clientHeight < 80;
      }}
    >
      <div className="column" role="log" aria-live="polite" aria-label="Conversation">
        {turns.map((turn) =>
          turn.role === "user" ? (
            <div key={turn.id} className="user" data-testid="user-turn">
              {turn.text}
            </div>
          ) : (
            <AssistantView key={turn.id} turn={turn} />
          )
        )}
        <div ref={endRef} />
      </div>
    </div>
  );
}

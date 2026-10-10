// The CLI's only screen: a scrolling transcript, a live activity log for
// the in-flight turn (tool calls/results as they stream in), and an
// input line. Deliberately one flat component for this v1 scaffold
// rather than a router/multi-screen setup - see packages/cli/README.md
// for what's intentionally not built yet.
import { useEffect, useRef, useState } from "react";
import { Box, Text, useApp } from "ink";
import TextInput from "ink-text-input";
import type { JSX } from "react";
import { AtlasClient, type AgentEvent, type RawSessionMessage } from "@atlas/client";

export interface AppProps {
  client: AtlasClient;
  sessionId: string;
  workspace?: string;
  serverLabel: string;
  // Explicit width (in columns) for the bordered banner/input boxes,
  // computed by Root from the real terminal size. Without this, Ink's
  // border-drawing box stretches to fill its flex parent but the plain
  // <Text> rows inside it don't, so the border ends up wider than the
  // content it's supposedly wrapping - this pins both to the same
  // number instead of relying on that stretch behavior. Optional with
  // a reasonable fallback so App still renders sensibly when used
  // outside Root (as the existing integration test does).
  width?: number;
  // Fired once per successfully completed turn (not on error) with the
  // user's message for that turn - lets the caller (cli.ts) persist the
  // session to the local picker registry without App needing to know
  // that registry exists.
  onTurnComplete?: (message: string) => void;
}

interface Turn {
  role: "user" | "assistant";
  content: string;
}

// Projects a session's complete raw history (every tool-call turn
// included - see RawSessionMessage's doc comment) down to the same
// user/assistant transcript the UI builds live, turn by turn, in
// submit() below. Kept in sync with that by construction: submit() only
// ever pushes a user Turn for what was typed and an assistant Turn for
// the FINAL reply - intermediate tool_call/tool_result/thinking steps
// are ephemeral liveEvents, never persisted into `history` - so
// reconstructing from storage has to apply the same filter, or a
// restored transcript would show raw tool-call noise a freshly-typed
// one never did. Exported (rather than kept private like describeEvent)
// specifically so this filter is unit-testable without rendering a
// component or standing up a server.
export function turnsFromHistory(messages: RawSessionMessage[]): Turn[] {
  const turns: Turn[] = [];
  for (const m of messages) {
    if (m.role === "user" && typeof m.content === "string") {
      turns.push({ role: "user", content: m.content });
    } else if (
      m.role === "assistant" &&
      (!m.tool_calls || m.tool_calls.length === 0) &&
      typeof m.content === "string" &&
      m.content.length > 0
    ) {
      turns.push({ role: "assistant", content: m.content });
    }
    // Everything else - "tool" messages, and "assistant" messages that
    // carry tool_calls - was only ever a live-only step, not part of
    // the transcript a user actually read turn by turn.
  }
  return turns;
}

const SPINNER_FRAMES = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];

function Spinner(): JSX.Element {
  const [frame, setFrame] = useState(0);
  useEffect(() => {
    const timer = setInterval(() => setFrame((f) => (f + 1) % SPINNER_FRAMES.length), 80);
    return () => clearInterval(timer);
  }, []);
  return <Text color="cyan">{SPINNER_FRAMES[frame]}</Text>;
}

// Renders one streamed AgentEvent as a single line of dim status text.
// "final"/"error" aren't expected here - the caller stops collecting live
// events and moves the turn into `history` (or shows `error`) as soon as
// either one arrives - but they're handled anyway so a protocol surprise
// degrades to a visible line instead of a silent gap.
function describeEvent(event: AgentEvent): string {
  switch (event.type) {
    case "iteration_start":
      return `→ iteration ${event.iteration}/${event.max_iterations}`;
    case "thinking":
      return `\u{1F4AD} ${event.content}`;
    case "assistant_thought":
      return event.content;
    case "tool_call":
      return `⚙ ${event.name}(${JSON.stringify(event.arguments)})`;
    case "tool_result":
      return `✓ ${event.name} -> ${JSON.stringify(event.result).slice(0, 200)}`;
    case "final":
      return event.reply;
    case "error":
      return `error: ${event.message}`;
    default:
      return JSON.stringify(event);
  }
}

export default function App({ client, sessionId, workspace, serverLabel, width = 76, onTurnComplete }: AppProps): JSX.Element {
  const { exit } = useApp();
  const [history, setHistory] = useState<Turn[]>([]);
  const [input, setInput] = useState("");
  const [busy, setBusy] = useState(false);
  const [liveEvents, setLiveEvents] = useState<AgentEvent[]>([]);
  const [serverOk, setServerOk] = useState<boolean | undefined>(undefined);
  const [error, setError] = useState<string | undefined>(undefined);
  // Transient one-line status for a connection retry or a streaming ->
  // non-streaming fallback, shown above the spinner while busy. Cleared
  // whenever a new turn starts or the current one finishes.
  const [statusLine, setStatusLine] = useState<string | undefined>(undefined);
  const mounted = useRef(true);

  useEffect(() => {
    mounted.current = true;
    void client.health().then((ok) => {
      if (mounted.current) setServerOk(ok);
    });
    return () => {
      mounted.current = false;
    };
  }, [client]);

  // Restores this session's prior transcript on mount. Needed because
  // Root unmounts/remounts App on every Tab press between the Chat and
  // Sessions views (and when SessionPicker hands back a different
  // sessionId) - `history` above is plain component state with nothing
  // backing it, so without this, switching to the Sessions tab and back
  // (or resuming an older session) silently wiped the transcript on
  // screen even though the real conversation was always safely
  // persisted server-side the whole time. getHistory() never throws
  // (see its doc comment in packages/client), so a fresh/never-seen
  // sessionId just resolves to an empty list here, same as today.
  useEffect(() => {
    void client.getHistory(sessionId).then((messages) => {
      if (!mounted.current) return;
      // Only fill in an empty transcript. If the user already sent a
      // message before this fetch came back, `h` has their live turn in
      // it and blindly replacing it would wipe what they just typed.
      setHistory((h) => (h.length > 0 ? h : turnsFromHistory(messages)));
    });
  }, [client, sessionId]);

  async function submit(message: string): Promise<void> {
    if (busy) return; // one turn at a time - see README's "what's not here yet"

    const trimmed = message.trim();
    setInput("");
    if (!trimmed) return;

    if (trimmed === "/exit" || trimmed === "/quit") {
      exit();
      return;
    }

    setHistory((h) => [...h, { role: "user", content: trimmed }]);
    setError(undefined);
    setLiveEvents([]);
    setStatusLine(undefined);
    setBusy(true);

    try {
      // sendMessage() (rather than chatStream() directly) retries a
      // dropped /chat/stream connection with backoff, and falls back to
      // the non-streaming /chat if streaming still can't connect - see
      // its doc comment in packages/client for exactly which failures that
      // covers and why (it's narrower than "any dropped connection", on
      // purpose - see issue #14/#15 discussion in the PR that added it).
      const reply = await client.sendMessage(
        { sessionId, message: trimmed, workspace },
        (event) => {
          if (!mounted.current) return;
          setLiveEvents((events) => [...events, event]);
        },
        {
          onRetry: ({ attempt }) => {
            if (!mounted.current) return;
            setStatusLine(`connection dropped, reconnecting (attempt ${attempt})...`);
          },
          onFallback: () => {
            if (!mounted.current) return;
            setStatusLine("streaming unavailable, falling back to a single reply...");
          },
        }
      );
      if (!mounted.current) return;
      setHistory((h) => [...h, { role: "assistant", content: reply }]);
      onTurnComplete?.(trimmed);
    } catch (err) {
      if (!mounted.current) return;
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      if (mounted.current) {
        setBusy(false);
        setLiveEvents([]);
        setStatusLine(undefined);
      }
    }
  }

  return (
    <Box flexDirection="column" flexGrow={1}>
      <Box flexDirection="column" borderStyle="round" borderColor="cyan" paddingX={2} paddingY={0} marginBottom={1} width={width}>
        <Text bold color="cyan">
          ATLAS
        </Text>
        <Text dimColor>Local AI workspace agent - command-line interface</Text>
        <Box marginTop={1} flexDirection="column">
          <Text>
            <Text color={serverOk === false ? "red" : "green"}>{"\u25cf"}</Text>
            <Text dimColor>
              {" "}
              {serverOk === false ? `Atlas server unreachable at ${serverLabel}` : `Connected to Atlas server at ${serverLabel}`}
            </Text>
          </Text>
          <Text>
            <Text color="green">{"\u25cf"}</Text>
            <Text dimColor> Workspace: {workspace ?? "default"}</Text>
          </Text>
          <Text>
            <Text color="green">{"\u25cf"}</Text>
            <Text dimColor> Session: {sessionId}</Text>
          </Text>
        </Box>
      </Box>

      <Box flexDirection="column" flexGrow={1}>
      {history.map((turn, i) => (
        <Box key={i} marginBottom={turn.role === "assistant" ? 1 : 0}>
          <Text color={turn.role === "user" ? "green" : "white"} bold={turn.role === "user"}>
            {turn.role === "user" ? "you> " : "atlas> "}
          </Text>
          <Text>{turn.content}</Text>
        </Box>
      ))}

      {busy && (
        <Box flexDirection="column" marginBottom={1}>
          {liveEvents.map((event, i) => (
            <Box key={i}>
              <Text dimColor>{describeEvent(event)}</Text>
            </Box>
          ))}
          {statusLine && (
            <Box>
              <Text color="yellow">{statusLine}</Text>
            </Box>
          )}
          <Box>
            <Spinner />
            <Text dimColor> working...</Text>
          </Box>
        </Box>
      )}

      {error && (
        <Box marginBottom={1}>
          <Text color="red">error: {error}</Text>
        </Box>
      )}
      </Box>

      <Box borderStyle="round" borderColor={busy ? "yellow" : "gray"} paddingX={1} width={width}>
        <Text color="green" bold>
          {"> "}
        </Text>
        <TextInput
          value={input}
          onChange={setInput}
          onSubmit={(v) => void submit(v)}
          showCursor={!busy}
          placeholder="Type a message, or /exit to quit"
        />
      </Box>
    </Box>
  );
}

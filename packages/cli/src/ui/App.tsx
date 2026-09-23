// The CLI's only screen: a scrolling transcript, a live activity log for
// the in-flight turn (tool calls/results as they stream in), and an
// input line. Deliberately one flat component for this v1 scaffold
// rather than a router/multi-screen setup - see packages/cli/README.md
// for what's intentionally not built yet.
import { useEffect, useRef, useState } from "react";
import { Box, Text, useApp } from "ink";
import TextInput from "ink-text-input";
import type { JSX } from "react";
import { AtlasClient, type AgentEvent } from "../api/client.js";

export interface AppProps {
  client: AtlasClient;
  sessionId: string;
  workspace?: string;
  serverLabel: string;
}

interface Turn {
  role: "user" | "assistant";
  content: string;
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

export default function App({ client, sessionId, workspace, serverLabel }: AppProps): JSX.Element {
  const { exit } = useApp();
  const [history, setHistory] = useState<Turn[]>([]);
  const [input, setInput] = useState("");
  const [busy, setBusy] = useState(false);
  const [liveEvents, setLiveEvents] = useState<AgentEvent[]>([]);
  const [serverOk, setServerOk] = useState<boolean | undefined>(undefined);
  const [error, setError] = useState<string | undefined>(undefined);
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
    setBusy(true);

    try {
      const reply = await client.chatStream({ sessionId, message: trimmed, workspace }, (event) => {
        if (!mounted.current) return;
        setLiveEvents((events) => [...events, event]);
      });
      if (!mounted.current) return;
      setHistory((h) => [...h, { role: "assistant", content: reply }]);
    } catch (err) {
      if (!mounted.current) return;
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      if (mounted.current) {
        setBusy(false);
        setLiveEvents([]);
      }
    }
  }

  return (
    <Box flexDirection="column">
      <Box marginBottom={1}>
        <Text bold color="cyan">
          Atlas
        </Text>
        <Text dimColor>
          {" "}
          - {serverLabel} - session {sessionId}
        </Text>
        {serverOk === false && <Text color="red"> (server unreachable)</Text>}
      </Box>

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

      <Box>
        <Text color="green" bold>
          you&gt;{" "}
        </Text>
        <TextInput value={input} onChange={setInput} onSubmit={(v) => void submit(v)} showCursor={!busy} />
      </Box>
    </Box>
  );
}

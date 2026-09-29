// Top-level shell: owns which tab is active ("chat" or "sessions") and
// renders a persistent tab bar + footer around whichever tab's content.
// Replaces the old flow where the session picker was a separate
// full-unmount screen shown once before App ever rendered - now
// switching sessions is just another tab, reachable at any time with
// Tab, and nothing about the running chat is torn down to get there.
import { useEffect, useState } from "react";
import { randomUUID } from "node:crypto";
import { Box, Text, useInput } from "ink";
import type { JSX } from "react";
import type { AtlasClient } from "../api/client.js";
import { recordTurn, sessionsFor, type SessionRecord } from "../config/sessions.js";
import App from "./App.js";
import SessionPicker from "./SessionPicker.js";

export type TabId = "chat" | "sessions";

export interface RootProps {
  client: AtlasClient;
  initialSessionId: string;
  initialTab: TabId;
  server: string;
  workspace: string;
}

function Tab({ label, active }: { label: string; active: boolean }): JSX.Element {
  return (
    <Text bold color={active ? "black" : "cyan"} backgroundColor={active ? "cyan" : undefined}>
      {` ${label} `}
    </Text>
  );
}

export default function Root({ client, initialSessionId, initialTab, server, workspace }: RootProps): JSX.Element {
  const [tab, setTab] = useState<TabId>(initialTab);
  const [sessionId, setSessionId] = useState(initialSessionId);
  // Re-read on every switch to the Sessions tab (not just once at
  // startup) so a session recorded earlier in this same run shows up
  // immediately, not just after restarting the CLI.
  const [sessions, setSessions] = useState<SessionRecord[]>(() => sessionsFor(server, workspace));

  // Sizes the whole app to the real terminal, not a fixed guess - this
  // is what makes it read as an actual full-screen app rather than a
  // small card floating in the corner of a big window. Falls back to a
  // conservative 80x24 when stdout isn't a real TTY (piped output, some
  // test runners) where .columns/.rows are undefined.
  const [dimensions, setDimensions] = useState({
    columns: process.stdout.columns || 80,
    rows: process.stdout.rows || 24,
  });
  useEffect(() => {
    function onResize(): void {
      setDimensions({ columns: process.stdout.columns || 80, rows: process.stdout.rows || 24 });
    }
    process.stdout.on("resize", onResize);
    return () => {
      process.stdout.off("resize", onResize);
    };
  }, []);

  useInput((_input, key) => {
    if (key.tab) {
      setTab((t) => {
        const next = t === "chat" ? "sessions" : "chat";
        if (next === "sessions") setSessions(sessionsFor(server, workspace));
        return next;
      });
    }
  });

  // Root itself adds 1 column of paddingX on each side below - the
  // content width passed to App has to account for that, or its
  // borders would run 2 columns past the terminal edge.
  const contentWidth = Math.max(dimensions.columns - 2, 20);

  return (
    <Box flexDirection="column" width={dimensions.columns} height={dimensions.rows} paddingX={1}>
      <Box marginBottom={1}>
        <Tab label="Chat" active={tab === "chat"} />
        <Text> </Text>
        <Tab label="Sessions" active={tab === "sessions"} />
      </Box>

      <Box flexDirection="column" flexGrow={1}>
        {tab === "chat" ? (
          <App
            client={client}
            sessionId={sessionId}
            workspace={workspace}
            serverLabel={server}
            width={contentWidth}
            onTurnComplete={(message) => {
              recordTurn({ id: sessionId, server, workspace, message });
            }}
          />
        ) : (
          <SessionPicker
            sessions={sessions}
            onSelect={(id) => {
              setSessionId(id ?? randomUUID());
              setTab("chat");
            }}
          />
        )}
      </Box>

      <Box marginTop={1}>
        <Text dimColor>
          {tab === "sessions"
            ? "↑↓ select · Enter choose · Tab switch view · Ctrl+C exit"
            : "Tab switch view · Ctrl+C exit · /exit or /quit to leave"}
        </Text>
      </Box>
    </Box>
  );
}

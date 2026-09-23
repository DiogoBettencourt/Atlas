// Shown at startup when there's at least one known session for the
// current --server/--workspace (see cli.ts) - lets the user resume one
// instead of always starting fresh and having to remember a session id.
// Skipped entirely on first run (no sessions yet) or with --new/--session.
import { useState } from "react";
import { Box, Text, useInput } from "ink";
import type { JSX } from "react";
import type { SessionRecord } from "../config/sessions.js";

export interface SessionPickerProps {
  sessions: SessionRecord[]; // expected pre-sorted, most recent first
  onSelect: (sessionId: string | undefined) => void; // undefined = start new
}

function relativeTime(iso: string): string {
  const minutes = Math.round((Date.now() - new Date(iso).getTime()) / 60000);
  if (minutes < 1) return "just now";
  if (minutes < 60) return `${minutes}m ago`;
  const hours = Math.round(minutes / 60);
  if (hours < 24) return `${hours}h ago`;
  return `${Math.round(hours / 24)}d ago`;
}

export default function SessionPicker({ sessions, onSelect }: SessionPickerProps): JSX.Element {
  // Row 0 is always "start a new session"; rows 1..n are existing
  // sessions in the order they were given.
  const [index, setIndex] = useState(0);
  const rowCount = sessions.length + 1;

  useInput((_input, key) => {
    if (key.upArrow) setIndex((i) => (i - 1 + rowCount) % rowCount);
    else if (key.downArrow) setIndex((i) => (i + 1) % rowCount);
    else if (key.return) onSelect(index === 0 ? undefined : sessions[index - 1]?.id);
  });

  return (
    <Box flexDirection="column">
      <Box marginBottom={1}>
        <Text bold color="cyan">
          Atlas
        </Text>
        <Text dimColor> - choose a session (↑↓ then Enter)</Text>
      </Box>

      <Box>
        <Text color={index === 0 ? "green" : undefined} bold={index === 0}>
          {index === 0 ? "› " : "  "}+ start a new session
        </Text>
      </Box>

      {sessions.map((session, i) => {
        const row = i + 1;
        const selected = row === index;
        return (
          <Box key={session.id}>
            <Text color={selected ? "green" : undefined} bold={selected}>
              {selected ? "› " : "  "}
              {session.label || "(no message yet)"}
            </Text>
            <Text dimColor>
              {"  "}
              {relativeTime(session.updatedAt)} · {session.messageCount} msg
              {session.messageCount === 1 ? "" : "s"}
            </Text>
          </Box>
        );
      })}
    </Box>
  );
}

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
  // Fired after the user confirms deleting a session (see the inline
  // y/n prompt below) - the caller owns actually deleting it (server
  // call + local registry update); this component only asks for
  // confirmation and reports the decision.
  onDelete: (sessionId: string) => void;
}

function relativeTime(iso: string): string {
  const minutes = Math.round((Date.now() - new Date(iso).getTime()) / 60000);
  if (minutes < 1) return "just now";
  if (minutes < 60) return `${minutes}m ago`;
  const hours = Math.round(minutes / 60);
  if (hours < 24) return `${hours}h ago`;
  return `${Math.round(hours / 24)}d ago`;
}

export default function SessionPicker({ sessions, onSelect, onDelete }: SessionPickerProps): JSX.Element {
  // Row 0 is always "start a new session"; rows 1..n are existing
  // sessions in the order they were given.
  const [index, setIndex] = useState(0);
  const rowCount = sessions.length + 1;
  // Id of the session currently showing an inline "delete this? (y/n)"
  // prompt in place of its normal row, or undefined when none is. A
  // single stray 'd' keypress deleting a whole conversation with no way
  // back would be a bad trade for saving one keystroke, so this is a
  // deliberate two-step confirm rather than an immediate delete.
  const [confirmingId, setConfirmingId] = useState<string | undefined>(undefined);

  useInput((input, key) => {
    if (confirmingId !== undefined) {
      // Deliberately narrow while a confirm is pending: only an
      // explicit yes commits the delete, and everything else - 'n',
      // Escape, or any other key at all - backs out without acting,
      // rather than letting arrow keys/Enter slip through and land on
      // the wrong row once the list re-renders one row shorter.
      if (input.toLowerCase() === "y") {
        onDelete(confirmingId);
        setConfirmingId(undefined);
        // `sessions` here still includes the row just deleted (the
        // parent hasn't re-rendered with the shorter list yet), so its
        // length is exactly the new, post-delete rowCount - clamp into
        // that range now rather than leaving `index` pointing one past
        // the end until the next arrow-key press corrects it.
        setIndex((i) => Math.min(i, sessions.length - 1));
      } else {
        setConfirmingId(undefined);
      }
      return;
    }

    if (key.upArrow) setIndex((i) => (i - 1 + rowCount) % rowCount);
    else if (key.downArrow) setIndex((i) => (i + 1) % rowCount);
    else if (key.return) onSelect(index === 0 ? undefined : sessions[index - 1]?.id);
    else if (input.toLowerCase() === "d" && index > 0) {
      const target = sessions[index - 1];
      if (target) setConfirmingId(target.id);
    }
  });

  return (
    <Box flexDirection="column">
      <Box>
        <Text color={index === 0 ? "green" : undefined} bold={index === 0}>
          {index === 0 ? "› " : "  "}+ start a new session
        </Text>
      </Box>

      {sessions.map((session, i) => {
        const row = i + 1;
        const selected = row === index;
        if (confirmingId === session.id) {
          return (
            <Box key={session.id}>
              <Text color="red" bold>
                {"› "}Delete "{session.label || "(no message yet)"}"? (y/n)
              </Text>
            </Box>
          );
        }
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

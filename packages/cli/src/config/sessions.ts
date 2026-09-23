// A local registry of sessions the CLI has talked to, so the picker
// (ui/SessionPicker.tsx) has something to show instead of making the
// user memorize and retype a session id. This is purely a client-side
// index for convenience - the actual conversation history lives on the
// Atlas server (SessionManager), keyed by session_id; deleting this file
// loses the picker's list, never any conversation content.
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { configDir, sessionsFilePath } from "./paths.js";

export interface SessionRecord {
  id: string;
  server: string;
  workspace: string;
  label: string;
  messageCount: number;
  updatedAt: string; // ISO timestamp
}

// Bounds the registry file so it doesn't grow forever across months of
// use - the oldest sessions (by updatedAt) are dropped past this count.
// This only forgets them for the picker; nothing is deleted server-side.
const MAX_SESSIONS = 50;

function isSessionRecord(value: unknown): value is SessionRecord {
  if (typeof value !== "object" || value === null) return false;
  const v = value as Record<string, unknown>;
  return (
    typeof v.id === "string" &&
    typeof v.server === "string" &&
    typeof v.workspace === "string" &&
    typeof v.label === "string" &&
    typeof v.messageCount === "number" &&
    typeof v.updatedAt === "string"
  );
}

export function loadSessions(): SessionRecord[] {
  const path = sessionsFilePath();
  if (!existsSync(path)) return [];

  try {
    const parsed: unknown = JSON.parse(readFileSync(path, "utf8"));
    if (!Array.isArray(parsed)) return [];
    return parsed.filter(isSessionRecord);
  } catch (err) {
    process.stderr.write(
      `atlas: warning: couldn't read session history at ${path} (${
        err instanceof Error ? err.message : String(err)
      }), starting fresh\n`
    );
    return [];
  }
}

function writeSessions(sessions: SessionRecord[]): void {
  mkdirSync(configDir(), { recursive: true });
  writeFileSync(sessionsFilePath(), JSON.stringify(sessions, null, 2) + "\n", "utf8");
}

// Sessions for the given server+workspace pair, most recently used first
// - a session logged against a different server (you pointed --server
// somewhere else) or workspace never shows up here, since resuming it
// against the wrong one wouldn't make sense.
export function sessionsFor(server: string, workspace: string): SessionRecord[] {
  return loadSessions()
    .filter((s) => s.server === server && s.workspace === workspace)
    .sort((a, b) => b.updatedAt.localeCompare(a.updatedAt));
}

// Called once per completed turn. Creates the session's registry entry on
// its FIRST turn only, using that turn's message as the picker's display
// label - later turns never overwrite the label (so a resumed session
// keeps showing what it was originally about), but always bump
// updatedAt/messageCount so recency sorting and the message count stay
// accurate.
export function recordTurn(params: { id: string; server: string; workspace: string; message: string }): void {
  const sessions = loadSessions();
  const existing = sessions.find((s) => s.id === params.id);
  const now = new Date().toISOString();

  if (existing) {
    existing.updatedAt = now;
    existing.messageCount += 1;
  } else {
    sessions.push({
      id: params.id,
      server: params.server,
      workspace: params.workspace,
      label: params.message.slice(0, 60),
      messageCount: 1,
      updatedAt: now,
    });
  }

  sessions.sort((a, b) => b.updatedAt.localeCompare(a.updatedAt));
  writeSessions(sessions.slice(0, MAX_SESSIONS));
}

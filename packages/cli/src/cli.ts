#!/usr/bin/env node
// Entry point: loads local config, parses flags, resolves which session
// to use (a specific one, a fresh one, or an interactively picked one),
// and renders the Ink app. Kept as thin as reasonably possible - wire
// format lives in api/client.ts, local state in config/, interaction in
// ui/.
import { randomUUID } from "node:crypto";
import { Command } from "commander";
import { render } from "ink";
import React from "react";
import { AtlasClient } from "./api/client.js";
import { loadConfig, resolveDefaults } from "./config/config.js";
import { recordTurn, sessionsFor, type SessionRecord } from "./config/sessions.js";
import App from "./ui/App.js";
import SessionPicker from "./ui/SessionPicker.js";

interface CliOptions {
  server: string;
  workspace: string;
  session?: string;
  new?: boolean;
  listSessions?: boolean;
}

const configDefaults = resolveDefaults(loadConfig());

const program = new Command();
program
  .name("atlas")
  .description("Terminal client for the Atlas agent - a streaming REPL over its REST API.")
  .option("-s, --server <url>", "Atlas API server base URL", configDefaults.server)
  .option("-w, --workspace <name>", "workspace name on the server", configDefaults.workspace)
  .option("--session <id>", "resume a specific session id, skipping the picker")
  .option("--new", "start a fresh session, skipping the picker")
  .option("--list-sessions", "print known sessions for --server/--workspace and exit")
  .parse(process.argv);

const options = program.opts<CliOptions>();

// Renders the picker and resolves once the user has chosen - either an
// existing session id, or undefined to start a new one.
function pickSession(sessions: SessionRecord[]): Promise<string | undefined> {
  return new Promise((resolve) => {
    const { unmount } = render(
      React.createElement(SessionPicker, {
        sessions,
        onSelect: (id) => {
          unmount();
          resolve(id);
        },
      })
    );
  });
}

async function resolveSessionId(): Promise<string> {
  if (options.session) return options.session;
  if (options.new) return randomUUID();

  const candidates = sessionsFor(options.server, options.workspace);
  if (candidates.length === 0) return randomUUID(); // nothing to pick from yet - skip straight in

  const picked = await pickSession(candidates);
  return picked ?? randomUUID();
}

async function main(): Promise<void> {
  if (options.listSessions) {
    const candidates = sessionsFor(options.server, options.workspace);
    if (candidates.length === 0) {
      console.log(`No sessions yet for ${options.server} (workspace: ${options.workspace}).`);
    } else {
      console.log(`Sessions for ${options.server} (workspace: ${options.workspace}):\n`);
      for (const s of candidates) {
        const msgs = `${s.messageCount} msg${s.messageCount === 1 ? "" : "s"}`;
        console.log(`  ${s.id}  ${s.updatedAt}  (${msgs})  ${s.label}`);
      }
    }
    return;
  }

  const sessionId = await resolveSessionId();
  const client = new AtlasClient({ baseUrl: options.server });

  render(
    React.createElement(App, {
      client,
      sessionId,
      workspace: options.workspace,
      serverLabel: options.server,
      onTurnComplete: (message: string) => {
        recordTurn({ id: sessionId, server: options.server, workspace: options.workspace, message });
      },
    })
  );
}

main().catch((err: unknown) => {
  console.error(err instanceof Error ? err.message : String(err));
  process.exitCode = 1;
});

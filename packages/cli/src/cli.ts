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
import { sessionsFor } from "./config/sessions.js";
import { launchServer, ServerLaunchError } from "./server/launch.js";
import { configDir } from "./config/paths.js";
import { join } from "node:path";
import Root, { type TabId } from "./ui/Root.js";
import { VERSION } from "./version.js";

interface CliOptions {
  server: string;
  workspace: string;
  session?: string;
  new?: boolean;
  listSessions?: boolean;
  atlasBinary?: string;
}

const rawConfig = loadConfig();
const configDefaults = resolveDefaults(rawConfig);

const program = new Command();
program
  .name("atlas")
  .description("Terminal client for the Atlas agent - a streaming REPL over its REST API.")
  .version(VERSION, "-v, --version", "print the AtlasCLI version and exit")
  .option("-s, --server <url>", "Atlas API server base URL", configDefaults.server)
  .option("-w, --workspace <name>", "workspace name on the server", configDefaults.workspace)
  .option("--session <id>", "resume a specific session id, skipping the picker")
  .option("--new", "start a fresh session, skipping the picker")
  .option("--list-sessions", "print known sessions for --server/--workspace and exit")
  .option(
    "--atlas-binary <path>",
    "path to the atlas server binary - launch it automatically when --server is unreachable",
    rawConfig.atlasBinary
  )
  .parse(process.argv);

const options = program.opts<CliOptions>();

// Resolves which session to start on, and which tab to land in. A
// specific --session or --new always goes straight to the Chat tab -
// only the ambiguous "no flag given" case opens on the Sessions tab
// (mirroring the old picker-first behavior), and only when there's
// actually something to pick from.
function resolveStart(): { sessionId: string; tab: TabId } {
  if (options.session) return { sessionId: options.session, tab: "chat" };
  if (options.new) return { sessionId: randomUUID(), tab: "chat" };

  const candidates = sessionsFor(options.server, options.workspace);
  // The placeholder id here is never shown - the Sessions tab replaces
  // it the moment something is picked, or the user starts typing after
  // switching back to Chat without picking, in which case it's exactly
  // what --new would have generated anyway.
  if (candidates.length === 0) return { sessionId: randomUUID(), tab: "chat" };
  return { sessionId: randomUUID(), tab: "sessions" };
}

// Takes over the whole terminal (alternate screen buffer, like less,
// vim, or Claude Code's own TUI) instead of scrolling in the normal
// buffer, and always hands the terminal back in a clean state -
// including on Ctrl+C or an uncaught crash - so a broken exit never
// leaves the user's shell stuck on a blank alt-screen with a hidden
// cursor.
const ENTER_ALT_SCREEN = "\x1b[?1049h";
const EXIT_ALT_SCREEN = "\x1b[?1049l";
const HIDE_CURSOR = "\x1b[?25l";
const SHOW_CURSOR = "\x1b[?25h";

// Set once launchServer() (below) succeeds, so the exit/SIGINT handlers
// can stop the server the CLI itself started - never a server the user
// was already running, since serverChild only exists on the auto-start
// path.
let serverChild: import("node:child_process").ChildProcess | undefined;

function restoreTerminal(): void {
  process.stdout.write(SHOW_CURSOR + EXIT_ALT_SCREEN);
  serverChild?.kill();
}

// Best-effort auto-start: only attempted when --atlas-binary is
// configured AND the configured --server doesn't already answer a
// health check (so a server the user is already running, or started
// themselves, is always left alone). Failures here are reported but
// never fatal - the CLI still renders normally afterward, same as if
// auto-start weren't configured at all, and the existing "server
// unreachable" state in App.tsx is what the user sees.
async function maybeAutoStartServer(client: AtlasClient): Promise<void> {
  if (!options.atlasBinary) return;
  if (await client.health()) return;

  try {
    const { child } = await launchServer({
      serverUrl: options.server,
      binaryPath: options.atlasBinary,
      logFilePath: join(configDir(), "atlas-server.log"),
      checkHealth: (url) => new AtlasClient({ baseUrl: url }).health(),
    });
    serverChild = child;
  } catch (err) {
    const message = err instanceof ServerLaunchError ? err.message : err instanceof Error ? err.message : String(err);
    process.stderr.write(`atlas: warning: couldn't auto-start the server (${message}) - continuing without it\n`);
  }
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

  const { sessionId, tab } = resolveStart();
  const client = new AtlasClient({ baseUrl: options.server });

  await maybeAutoStartServer(client);

  process.on("exit", restoreTerminal);
  process.on("SIGINT", () => {
    restoreTerminal();
    process.exit(130);
  });
  process.stdout.write(ENTER_ALT_SCREEN + HIDE_CURSOR);

  render(
    React.createElement(Root, {
      client,
      initialSessionId: sessionId,
      initialTab: tab,
      server: options.server,
      workspace: options.workspace,
    })
  );
}

main().catch((err: unknown) => {
  restoreTerminal();
  console.error(err instanceof Error ? err.message : String(err));
  process.exitCode = 1;
});

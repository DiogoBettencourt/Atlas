#!/usr/bin/env node
// Entry point: parses flags, wires up an AtlasClient, and renders the
// Ink app. Kept deliberately thin - all the actual behavior lives in
// api/client.ts (wire format) and ui/App.tsx (interaction).
import { randomUUID } from "node:crypto";
import { Command } from "commander";
import { render } from "ink";
import React from "react";
import { AtlasClient } from "./api/client.js";
import App from "./ui/App.js";

interface CliOptions {
  server: string;
  session?: string;
  workspace: string;
}

const program = new Command();
program
  .name("atlas")
  .description("Terminal client for the Atlas agent - a streaming REPL over its REST API.")
  .option("-s, --server <url>", "Atlas API server base URL", "http://127.0.0.1:8080")
  .option("--session <id>", "session id to resume (defaults to a new random one)")
  .option("-w, --workspace <name>", "workspace name on the server", "default")
  .parse(process.argv);

const options = program.opts<CliOptions>();
const sessionId = options.session ?? randomUUID();
const client = new AtlasClient({ baseUrl: options.server });

render(
  React.createElement(App, {
    client,
    sessionId,
    workspace: options.workspace,
    serverLabel: options.server,
  })
);

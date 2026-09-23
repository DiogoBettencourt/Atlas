// Persisted CLI defaults (--server/--workspace) so a returning user
// doesn't have to retype flags every session. A CLI flag always wins over
// this file; this file always wins over the hardcoded fallback - see
// resolveDefaults().
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { configDir, configFilePath } from "./paths.js";

export interface AtlasConfig {
  server?: string;
  workspace?: string;
}

const HARDCODED_DEFAULTS: Required<AtlasConfig> = {
  server: "http://127.0.0.1:8080",
  workspace: "default",
};

// Never throws: a missing config file is just "no config yet" (returns
// {}), and a corrupt one is reported to stderr and treated the same way
// rather than crashing the whole CLI over a bad settings file.
export function loadConfig(): AtlasConfig {
  const path = configFilePath();
  if (!existsSync(path)) return {};

  try {
    const parsed: unknown = JSON.parse(readFileSync(path, "utf8"));
    if (typeof parsed !== "object" || parsed === null) return {};

    const obj = parsed as Record<string, unknown>;
    const config: AtlasConfig = {};
    if (typeof obj.server === "string") config.server = obj.server;
    if (typeof obj.workspace === "string") config.workspace = obj.workspace;
    return config;
  } catch (err) {
    process.stderr.write(
      `atlas: warning: couldn't read config at ${path} (${
        err instanceof Error ? err.message : String(err)
      }), using defaults\n`
    );
    return {};
  }
}

export function saveConfig(config: AtlasConfig): void {
  mkdirSync(configDir(), { recursive: true });
  writeFileSync(configFilePath(), JSON.stringify(config, null, 2) + "\n", "utf8");
}

export function resolveDefaults(config: AtlasConfig): Required<AtlasConfig> {
  return {
    server: config.server ?? HARDCODED_DEFAULTS.server,
    workspace: config.workspace ?? HARDCODED_DEFAULTS.workspace,
  };
}

// Persisted CLI defaults (--server/--workspace) so a returning user
// doesn't have to retype flags every session. A CLI flag always wins over
// this file; this file always wins over the hardcoded fallback - see
// resolveDefaults().
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { configDir, configFilePath } from "./paths.js";

export interface AtlasConfig {
  server?: string;
  workspace?: string;
  // Path to the atlas server binary (e.g. an absolute path to
  // atlas.exe / atlas). When set, and only when the configured
  // --server isn't already reachable, the CLI launches it itself
  // instead of requiring a separate terminal window - see
  // src/server/launch.ts. No hardcoded default: unlike server/
  // workspace there's no sensible universal guess for where this
  // binary lives, so leaving it unset simply means auto-start is
  // never attempted (the previous, still-fully-supported behavior).
  atlasBinary?: string;
}

// Only server/workspace have a sensible universal default - see the
// doc comment on AtlasConfig.atlasBinary for why that one doesn't.
type CoreDefaults = Required<Pick<AtlasConfig, "server" | "workspace">>;

const HARDCODED_DEFAULTS: CoreDefaults = {
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
    if (typeof obj.atlasBinary === "string") config.atlasBinary = obj.atlasBinary;
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

export function resolveDefaults(config: AtlasConfig): CoreDefaults {
  return {
    server: config.server ?? HARDCODED_DEFAULTS.server,
    workspace: config.workspace ?? HARDCODED_DEFAULTS.workspace,
  };
}

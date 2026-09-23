// paths.ts reads ATLAS_CLI_CONFIG_DIR at call time (inside configDir()),
// not at module-load time, so a plain static import here is fine - each
// test just needs the env var set before it calls loadConfig/saveConfig.
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { loadConfig, resolveDefaults, saveConfig } from "./config.js";
import { configFilePath } from "./paths.js";

let dir: string;

beforeEach(() => {
  dir = mkdtempSync(join(tmpdir(), "atlas-cli-config-"));
  process.env.ATLAS_CLI_CONFIG_DIR = dir;
});

afterEach(() => {
  delete process.env.ATLAS_CLI_CONFIG_DIR;
  rmSync(dir, { recursive: true, force: true });
});

describe("loadConfig / saveConfig", () => {
  it("returns {} when no config file exists yet", () => {
    expect(loadConfig()).toEqual({});
  });

  it("round-trips through saveConfig", () => {
    saveConfig({ server: "http://example.com:9000", workspace: "scratch" });
    expect(loadConfig()).toEqual({ server: "http://example.com:9000", workspace: "scratch" });
  });

  it("falls back to {} (with a stderr warning, not a crash) on a corrupt file", () => {
    writeFileSync(configFilePath(), "{ not valid json", "utf8");
    expect(loadConfig()).toEqual({});
  });

  it("ignores non-string fields rather than passing them through", () => {
    writeFileSync(configFilePath(), JSON.stringify({ server: 12345, workspace: "ok" }), "utf8");
    expect(loadConfig()).toEqual({ workspace: "ok" });
  });
});

describe("resolveDefaults", () => {
  it("fills in the hardcoded fallback for anything the config doesn't set", () => {
    expect(resolveDefaults({})).toEqual({
      server: "http://127.0.0.1:8080",
      workspace: "default",
    });
    expect(resolveDefaults({ server: "http://example.com:9000" })).toEqual({
      server: "http://example.com:9000",
      workspace: "default",
    });
  });
});

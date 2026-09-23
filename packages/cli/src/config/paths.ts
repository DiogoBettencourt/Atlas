// Where AtlasCLI keeps its own local state: a config file (default
// --server/--workspace) and a session registry (for the picker). Platform-
// appropriate by default - %APPDATA%\atlas-cli on Windows (the primary
// dev machine this ships on), XDG_CONFIG_HOME/atlas-cli or ~/.config/
// atlas-cli elsewhere - and overridable via ATLAS_CLI_CONFIG_DIR so tests
// (and anyone who wants it) never touch a real home directory.
import { homedir } from "node:os";
import { join } from "node:path";

export function configDir(): string {
  const override = process.env.ATLAS_CLI_CONFIG_DIR;
  if (override) return override;

  if (process.platform === "win32") {
    return join(process.env.APPDATA ?? join(homedir(), "AppData", "Roaming"), "atlas-cli");
  }

  const xdg = process.env.XDG_CONFIG_HOME;
  return join(xdg ?? join(homedir(), ".config"), "atlas-cli");
}

export function configFilePath(): string {
  return join(configDir(), "config.json");
}

export function sessionsFilePath(): string {
  return join(configDir(), "sessions.json");
}

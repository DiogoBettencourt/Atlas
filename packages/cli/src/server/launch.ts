// Optionally launches the Atlas server itself before the CLI connects to
// it - opt-in via --atlas-binary (or the "atlasBinary" config field),
// and only attempted when --server isn't already reachable. This is
// deliberately best-effort: nothing here ever blocks the CLI from
// starting normally when auto-start isn't configured, and a failed
// launch just means the existing "server unreachable" banner in App.tsx
// is what the user sees, same as before this existed.
import { spawn, type ChildProcess, type SpawnOptions } from "node:child_process";
import { dirname } from "node:path";
import { mkdirSync, openSync } from "node:fs";

export class ServerLaunchError extends Error {}

export interface LaunchOptions {
  serverUrl: string; // e.g. http://127.0.0.1:8080 - only the port is used
  binaryPath: string;
  logFilePath: string;
  checkHealth: (url: string) => Promise<boolean>;
  spawnFn?: (command: string, args: string[], options: SpawnOptions) => ChildProcess;
  pollIntervalMs?: number;
  timeoutMs?: number;
}

export interface LaunchResult {
  child: ChildProcess;
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function portFromUrl(url: string): string {
  try {
    const parsed = new URL(url);
    return parsed.port || "8080";
  } catch {
    return "8080";
  }
}

// Spawns `binaryPath`, redirects its stdout/stderr to a log file (never
// to this process's own stdout/stderr - the CLI owns the terminal's
// alternate screen buffer for its own UI, and interleaved server output
// would corrupt it), then polls checkHealth() until it succeeds, the
// child exits early, or timeoutMs passes. Throws ServerLaunchError in
// the latter two cases and kills the child before doing so; on success,
// returns the still-running child so the caller can track and stop it.
export async function launchServer(options: LaunchOptions): Promise<LaunchResult> {
  const { serverUrl, binaryPath, logFilePath, checkHealth, spawnFn = spawn, pollIntervalMs = 300, timeoutMs = 15000 } = options;

  mkdirSync(dirname(logFilePath), { recursive: true });
  const logFd = openSync(logFilePath, "a");

  const child = spawnFn(binaryPath, [`--port=${portFromUrl(serverUrl)}`, "--bind=127.0.0.1"], {
    stdio: ["ignore", logFd, logFd],
  });

  let earlyFailure: Error | undefined;
  child.once("error", (err) => {
    earlyFailure = err;
  });
  child.once("exit", (code, signal) => {
    // A clean exit after we've already confirmed health (the caller is
    // done with launchServer() by then) isn't an "early" failure - only
    // record this while still polling below.
    earlyFailure ??= new Error(`atlas exited before becoming healthy (code ${String(code)}, signal ${String(signal)}) - see ${logFilePath}`);
  });

  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (earlyFailure) throw new ServerLaunchError(earlyFailure.message);
    if (await checkHealth(serverUrl)) return { child };
    await sleep(pollIntervalMs);
  }

  child.kill();
  throw new ServerLaunchError(`atlas server at ${serverUrl} didn't become healthy within ${timeoutMs}ms - see ${logFilePath}`);
}

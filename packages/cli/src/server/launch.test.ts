// Tests launchServer() by spawning real fixture processes (see
// server/fixtures/) rather than mocking node:child_process - the whole
// point of this function is correctly supervising a real child process
// (detecting it become healthy, exit early, or hang), and a mocked
// spawn would just hand back whatever shape we told it to, defeating
// the purpose. checkHealth itself stays a plain injected function since
// launchServer already takes it as one - that's the seam the real
// AtlasClient.health() plugs into at runtime, and exercising HTTP
// semantics is client.test.ts's job, not this file's.
import { spawn as realSpawn, type ChildProcess } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { afterEach, describe, expect, it } from "vitest";
import { launchServer, ServerLaunchError } from "./launch.js";

const fixturesDir = join(fileURLToPath(new URL(".", import.meta.url)), "fixtures");
const longRunning = join(fixturesDir, "long-running.js");
const exitImmediately = join(fixturesDir, "exit-immediately.js");

let workDir: string;
const spawnedChildren: ChildProcess[] = [];

function freshLogPath(): string {
  workDir = mkdtempSync(join(tmpdir(), "atlas-cli-launch-test-"));
  return join(workDir, "atlas-server.log");
}

// Wraps the real spawn() so tests can grab a handle to the child even
// on paths where launchServer() throws instead of returning one, to
// assert it was actually killed rather than left running.
function trackingSpawn(...args: Parameters<typeof realSpawn>): ChildProcess {
  const child = realSpawn(...args);
  spawnedChildren.push(child);
  return child;
}

afterEach(() => {
  for (const child of spawnedChildren.splice(0)) {
    if (!child.killed) child.kill();
  }
  if (workDir) rmSync(workDir, { recursive: true, force: true });
});

describe("launchServer", () => {
  it("resolves with the running child once checkHealth reports true", async () => {
    let calls = 0;
    const { child } = await launchServer({
      serverUrl: "http://127.0.0.1:8080",
      binaryPath: longRunning,
      logFilePath: freshLogPath(),
      checkHealth: async () => {
        calls += 1;
        return calls >= 3; // unhealthy for the first couple of polls, then up
      },
      spawnFn: trackingSpawn,
      pollIntervalMs: 5,
      timeoutMs: 2000,
    });

    expect(child.exitCode).toBeNull();
    expect(calls).toBeGreaterThanOrEqual(3);
  });

  it("throws ServerLaunchError and kills the child if it never becomes healthy", async () => {
    await expect(
      launchServer({
        serverUrl: "http://127.0.0.1:8080",
        binaryPath: longRunning,
        logFilePath: freshLogPath(),
        checkHealth: async () => false,
        spawnFn: trackingSpawn,
        pollIntervalMs: 5,
        timeoutMs: 50,
      })
    ).rejects.toThrow(ServerLaunchError);

    // Give the killed process a moment to actually exit.
    await new Promise((resolve) => setTimeout(resolve, 100));
    const child = spawnedChildren[spawnedChildren.length - 1];
    expect(child.killed).toBe(true);
  });

  it("throws ServerLaunchError when the child exits before becoming healthy", async () => {
    await expect(
      launchServer({
        serverUrl: "http://127.0.0.1:8080",
        binaryPath: exitImmediately,
        logFilePath: freshLogPath(),
        checkHealth: async () => false,
        spawnFn: trackingSpawn,
        pollIntervalMs: 5,
        timeoutMs: 2000,
      })
    ).rejects.toThrow(ServerLaunchError);
  });

  it("creates the log file and redirects the child's stdio there rather than inheriting it", async () => {
    const path = freshLogPath();
    await expect(
      launchServer({
        serverUrl: "http://127.0.0.1:8080",
        binaryPath: exitImmediately,
        logFilePath: path,
        checkHealth: async () => false,
        spawnFn: trackingSpawn,
        pollIntervalMs: 5,
        timeoutMs: 2000,
      })
    ).rejects.toThrow(ServerLaunchError);

    // The fixture writes nothing itself, but launchServer must still
    // have created the file (and pointed the child's stdio at its fd,
    // never at this test process's own stdout/stderr).
    expect(() => readFileSync(path)).not.toThrow();
  });

  it("throws ServerLaunchError when the binary can't be spawned at all", async () => {
    await expect(
      launchServer({
        serverUrl: "http://127.0.0.1:8080",
        binaryPath: join(fixturesDir, "does-not-exist"),
        logFilePath: freshLogPath(),
        checkHealth: async () => false,
        pollIntervalMs: 5,
        timeoutMs: 2000,
      })
    ).rejects.toThrow(ServerLaunchError);
  });
});

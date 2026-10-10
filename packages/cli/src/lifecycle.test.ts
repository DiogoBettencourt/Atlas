import { describe, expect, it } from "vitest";
import { exitWhenDone } from "./lifecycle.js";

describe("exitWhenDone", () => {
  it("ends the process with 0 once the app has exited (so /exit really quits)", async () => {
    const codes: number[] = [];
    let finish: () => void = () => {};
    const done = exitWhenDone(
      () => new Promise<void>((resolve) => (finish = resolve)),
      (code) => codes.push(code)
    );
    // Still running: the process must not be ended early.
    await Promise.resolve();
    expect(codes).toEqual([]);
    finish();
    await done;
    expect(codes).toEqual([0]);
  });

  it("reports a crash and ends with 1 instead of hanging", async () => {
    const codes: number[] = [];
    const messages: string[] = [];
    await exitWhenDone(
      () => Promise.reject(new Error("render blew up")),
      (code) => codes.push(code),
      (m) => messages.push(m)
    );
    expect(codes).toEqual([1]);
    expect(messages).toEqual(["render blew up"]);
  });
});

import { describe, expect, it } from "vitest";
import { formatArgs, formatResult, relativeTime } from "./format";

describe("relativeTime", () => {
  const now = new Date(2026, 9, 10, 15, 0, 0); // local time, 10 Oct 2026 15:00

  it("handles missing and invalid input", () => {
    expect(relativeTime("", now)).toBe("");
    expect(relativeTime("not a date", now)).toBe("");
  });

  it("uses Just now, minutes, Today, Yesterday", () => {
    expect(relativeTime(new Date(2026, 9, 10, 14, 59, 30).toISOString(), now)).toBe("Just now");
    expect(relativeTime(new Date(2026, 9, 10, 14, 30, 0).toISOString(), now)).toBe("30 min ago");
    expect(relativeTime(new Date(2026, 9, 10, 9, 0, 0).toISOString(), now)).toBe("Today");
    expect(relativeTime(new Date(2026, 9, 9, 23, 0, 0).toISOString(), now)).toBe("Yesterday");
  });

  it("uses a weekday within a week and a date after that", () => {
    const three = relativeTime(new Date(2026, 9, 7, 12, 0, 0).toISOString(), now);
    expect(three).toBe(new Date(2026, 9, 7).toLocaleDateString(undefined, { weekday: "long" }));
    const old = relativeTime(new Date(2026, 8, 1, 12, 0, 0).toISOString(), now);
    expect(old).toBe(new Date(2026, 8, 1).toLocaleDateString(undefined, { month: "short", day: "numeric" }));
  });
});

describe("formatArgs", () => {
  it("renders object arguments as key: value pairs", () => {
    expect(formatArgs({ path: "ROADMAP.md", limit: 5 })).toBe('path: "ROADMAP.md", limit: 5');
  });
  it("handles undefined, strings and long values", () => {
    expect(formatArgs(undefined)).toBe("");
    expect(formatArgs("raw")).toBe("raw");
    const long = formatArgs({ q: "x".repeat(200) }, 30);
    expect(long.length).toBe(30);
    expect(long.endsWith("…")).toBe(true);
  });
});

describe("formatResult", () => {
  it("pretty-prints objects and passes strings through", () => {
    expect(formatResult("hi")).toBe("hi");
    expect(formatResult({ a: 1 })).toBe('{\n  "a": 1\n}');
  });
  it("truncates huge results and says how much was cut", () => {
    const out = formatResult("y".repeat(50), 10);
    expect(out.startsWith("y".repeat(10))).toBe(true);
    expect(out).toContain("40 more characters");
  });
});

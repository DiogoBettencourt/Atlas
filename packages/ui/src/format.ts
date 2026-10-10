// Small presentation helpers (kept pure so they can be tested without React).

// "Just now", "12 min ago", "Today", "Yesterday", a weekday for the last
// week, otherwise a short date. `now` is injectable for tests.
export function relativeTime(iso: string, now: Date = new Date()): string {
  if (!iso) return "";
  const then = new Date(iso);
  if (Number.isNaN(then.getTime())) return "";

  const diffMs = now.getTime() - then.getTime();
  if (diffMs < 60_000) return "Just now";
  if (diffMs < 3_600_000) return `${Math.floor(diffMs / 60_000)} min ago`;

  const startOfDay = (d: Date) => new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime();
  const dayDiff = Math.round((startOfDay(now) - startOfDay(then)) / 86_400_000);
  if (dayDiff <= 0) return "Today";
  if (dayDiff === 1) return "Yesterday";
  if (dayDiff < 7) return then.toLocaleDateString(undefined, { weekday: "long" });
  return then.toLocaleDateString(undefined, { month: "short", day: "numeric" });
}

// One-line preview of tool-call arguments: `path: "ROADMAP.md", limit: 50`.
export function formatArgs(args: unknown, max = 90): string {
  let text: string;
  if (args === undefined || args === null) {
    text = "";
  } else if (typeof args === "object" && !Array.isArray(args)) {
    text = Object.entries(args as Record<string, unknown>)
      .map(([key, value]) => `${key}: ${JSON.stringify(value)}`)
      .join(", ");
  } else {
    text = typeof args === "string" ? args : JSON.stringify(args);
  }
  return text.length > max ? `${text.slice(0, max - 1)}…` : text;
}

// A tool result as display text, shortened so one huge file read can't
// make the page unusable. The full value is still in the session history.
export function formatResult(result: unknown, max = 4000): string {
  const text = typeof result === "string" ? result : (JSON.stringify(result, null, 2) ?? "");
  return text.length > max ? `${text.slice(0, max)}\n… (${text.length - max} more characters)` : text;
}

export function newSessionId(): string {
  return crypto.randomUUID();
}

// Renders assistant text: fenced code blocks and `inline code`, everything
// else as plain text with line breaks kept. Deliberately not a markdown
// engine - no HTML is ever interpreted, so model output can't inject markup.
import type { ReactNode } from "react";

function stripFence(block: string): string {
  const inner = block.slice(3, -3);
  const newline = inner.indexOf("\n");
  // The first line of a fence is the language tag (```ts) when it has no spaces.
  if (newline >= 0 && /^[\w+#.-]*$/.test(inner.slice(0, newline).trim())) {
    return inner.slice(newline + 1).replace(/\n$/, "");
  }
  return inner.replace(/^\n/, "").replace(/\n$/, "");
}

function inline(text: string): ReactNode[] {
  return text.split(/(`[^`\n]+`)/g).map((part, i) =>
    part.length > 2 && part.startsWith("`") && part.endsWith("`") ? (
      <code key={i} className="inline-code">
        {part.slice(1, -1)}
      </code>
    ) : (
      part
    )
  );
}

export function RichText({ text }: { text: string }) {
  const parts = text.split(/(```[\s\S]*?```)/g).filter((p) => p.length > 0);
  return (
    <div className="rich">
      {parts.map((part, i) =>
        part.length >= 6 && part.startsWith("```") && part.endsWith("```") ? (
          <pre key={i} className="codeblock">
            <code>{stripFence(part)}</code>
          </pre>
        ) : (
          <p key={i}>{inline(part.replace(/^\n+|\n+$/g, ""))}</p>
        )
      )}
    </div>
  );
}

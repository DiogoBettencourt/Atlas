import { useEffect, useRef, useState } from "react";

interface Props {
  disabled: boolean;
  busy: boolean;
  onSend: (text: string) => void;
}

export function Composer({ disabled, busy, onSend }: Props) {
  const [text, setText] = useState("");
  const ref = useRef<HTMLTextAreaElement>(null);

  // Grow with the content up to a cap, then scroll inside.
  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    el.style.height = "auto";
    el.style.height = `${Math.min(el.scrollHeight, 200)}px`;
  }, [text]);

  const canSend = !disabled && !busy && text.trim().length > 0;

  const submit = () => {
    if (!canSend) return;
    onSend(text.trim());
    setText("");
  };

  return (
    <div className="composer-wrap">
      <form
        className="composer"
        onSubmit={(e) => {
          e.preventDefault();
          submit();
        }}
      >
        <label htmlFor="message" className="sr-only">
          Message Atlas
        </label>
        <textarea
          id="message"
          ref={ref}
          rows={1}
          placeholder="Message Atlas…"
          value={text}
          onChange={(e) => setText(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === "Enter" && !e.shiftKey && !e.nativeEvent.isComposing) {
              e.preventDefault();
              submit();
            }
          }}
        />
        <button type="submit" className="send" disabled={!canSend}>
          {busy ? (
            "Working…"
          ) : (
            <>
              <svg width="14" height="14" viewBox="0 0 14 14" fill="none" aria-hidden="true">
                <path d="M2 7h10M8 3l4 4-4 4" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" />
              </svg>
              Send
            </>
          )}
        </button>
      </form>
      <div className="hint">Enter to send · Shift+Enter for a new line</div>
    </div>
  );
}

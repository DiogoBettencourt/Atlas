import { useEffect, useRef, useState } from "react";

interface Props {
  disabled: boolean;
  busy: boolean;
  // A Stop has been asked for and the turn hasn't ended yet.
  stopping: boolean;
  onSend: (text: string) => void;
  onStop: () => void;
}

export function Composer({ disabled, busy, stopping, onSend, onStop }: Props) {
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
            if (e.key === "Escape" && busy && !stopping) {
              e.preventDefault();
              onStop();
            }
          }}
        />
        {busy ? (
          <button type="button" className="send stop" disabled={stopping} onClick={onStop}>
            {stopping ? (
              "Stopping…"
            ) : (
              <>
                <svg width="12" height="12" viewBox="0 0 12 12" fill="none" aria-hidden="true">
                  <rect x="2" y="2" width="8" height="8" rx="1.5" fill="currentColor" />
                </svg>
                Stop
              </>
            )}
          </button>
        ) : (
          <button type="submit" className="send" disabled={!canSend}>
            <svg width="14" height="14" viewBox="0 0 14 14" fill="none" aria-hidden="true">
              <path d="M2 7h10M8 3l4 4-4 4" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" />
            </svg>
            Send
          </button>
        )}
      </form>
      <div className="hint">{busy ? "Esc to stop" : "Enter to send · Shift+Enter for a new line"}</div>
    </div>
  );
}

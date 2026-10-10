import type { SessionSummary } from "@atlas/client";
import { relativeTime } from "./format";

interface Props {
  sessions: SessionSummary[];
  activeId: string;
  // True while the active session has no server-side record yet (a brand
  // new chat), so it is shown as a draft entry at the top.
  activeIsDraft: boolean;
  locked: boolean;
  online: boolean | null;
  serverLabel: string;
  version?: string;
  listError: string | null;
  open: boolean;
  onSelect: (id: string) => void;
  onNew: () => void;
  onDelete: (session: SessionSummary) => void;
  onClose: () => void;
}

function Logo() {
  return (
    <svg width="26" height="26" viewBox="0 0 26 26" fill="none" role="img" aria-label="Atlas">
      <circle cx="13" cy="13" r="11" stroke="var(--accent)" strokeWidth="2" />
      <path d="M13 2v22M2 13h22" stroke="var(--accent)" strokeWidth="1.5" opacity="0.5" />
      <circle cx="13" cy="13" r="3.5" fill="var(--accent)" />
    </svg>
  );
}

export function Sidebar(props: Props) {
  const { sessions, activeId, activeIsDraft, locked } = props;
  return (
    <aside className={`sidebar${props.open ? " sidebar-open" : ""}`} aria-label="Sidebar">
      <div className="brand">
        <Logo />
        <span>Atlas</span>
        <button type="button" className="icon-btn close-nav" aria-label="Close sidebar" onClick={props.onClose}>
          <svg width="16" height="16" viewBox="0 0 16 16" fill="none" aria-hidden="true">
            <path d="M4 4l8 8M12 4l-8 8" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" />
          </svg>
        </button>
      </div>

      <button type="button" className="btn new-chat" onClick={props.onNew} disabled={locked || activeIsDraft}>
        <svg width="16" height="16" viewBox="0 0 16 16" fill="none" aria-hidden="true">
          <path d="M8 3v10M3 8h10" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" />
        </svg>
        New chat
      </button>

      <nav aria-label="Sessions" className="sessions">
        <div className="sessions-label">Sessions</div>
        {props.listError && (
          <div className="list-error" role="alert">
            {props.listError}
          </div>
        )}
        {activeIsDraft && (
          <div className="session session-active">
            <button type="button" className="session-main" aria-current="true" disabled>
              <span className="session-title">New chat</span>
              <span className="session-time">Not saved yet</span>
            </button>
          </div>
        )}
        {sessions.map((s) => {
          const active = s.id === activeId;
          return (
            <div key={s.id} className={`session${active ? " session-active" : ""}`}>
              <button
                type="button"
                className="session-main"
                aria-current={active ? "true" : undefined}
                disabled={locked}
                onClick={() => props.onSelect(s.id)}
              >
                <span className="session-title">{s.title || "Untitled chat"}</span>
                <span className="session-time">{relativeTime(s.updatedAt)}</span>
              </button>
              <button
                type="button"
                className="icon-btn session-delete"
                aria-label={`Delete session: ${s.title || "Untitled chat"}`}
                disabled={locked}
                onClick={() => props.onDelete(s)}
              >
                <svg width="16" height="16" viewBox="0 0 16 16" fill="none" aria-hidden="true">
                  <path
                    d="M3 4.5h10M6.5 4.5V3h3v1.5M4.5 4.5l.6 8h5.8l.6-8"
                    stroke="currentColor"
                    strokeWidth="1.4"
                    strokeLinecap="round"
                    strokeLinejoin="round"
                  />
                </svg>
              </button>
            </div>
          );
        })}
        {!props.listError && sessions.length === 0 && !activeIsDraft && (
          <div className="sessions-empty">No sessions yet.</div>
        )}
      </nav>

      <div className="status">
        <span className={`status-dot ${props.online === null ? "unknown" : props.online ? "ok" : "bad"}`} />
        <span className="status-text">
          {props.online === null ? "Connecting…" : props.online ? "Connected" : "Offline"} · {props.serverLabel}
        </span>
        {props.version && <span className="status-version">v{props.version}</span>}
      </div>
    </aside>
  );
}

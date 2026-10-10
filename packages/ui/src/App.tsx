import { AtlasClient, type ServerInfo, type SessionSummary } from "@atlas/client";
import { useCallback, useEffect, useMemo, useState } from "react";
import { applyEvent, failTurn, newAssistantTurn, turnsFromHistory, type Turn } from "./chatState";
import { Composer } from "./Composer";
import { ConfirmDialog } from "./ConfirmDialog";
import { serverUrl } from "./env";
import { newSessionId } from "./format";
import { Sidebar } from "./Sidebar";
import { Transcript } from "./Transcript";

const STARTERS = ["Summarize this repository", "Find TODOs in src/", "Draft release notes"];
const HASH_PREFIX = "#/s/";

function readHash(): string | null {
  const hash = window.location.hash;
  return hash.startsWith(HASH_PREFIX) ? decodeURIComponent(hash.slice(HASH_PREFIX.length)) || null : null;
}

function errorText(err: unknown): string {
  return err instanceof Error ? err.message : String(err);
}

export default function App({ client: injected }: { client?: AtlasClient }) {
  const url = useMemo(() => serverUrl(), []);
  const client = useMemo(() => injected ?? new AtlasClient({ baseUrl: url }), [injected, url]);

  const [sessions, setSessions] = useState<SessionSummary[]>([]);
  const [listError, setListError] = useState<string | null>(null);
  const [online, setOnline] = useState<boolean | null>(null);
  const [info, setInfo] = useState<ServerInfo | null>(null);
  const [activeId, setActiveId] = useState<string>(() => readHash() ?? newSessionId());
  const [turns, setTurns] = useState<Turn[]>([]);
  const [loadingHistory, setLoadingHistory] = useState(true);
  const [busy, setBusy] = useState(false);
  const [pendingDelete, setPendingDelete] = useState<SessionSummary | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [navOpen, setNavOpen] = useState(false);

  const refreshSessions = useCallback(async () => {
    try {
      setSessions(await client.listSessions());
      setListError(null);
    } catch (err) {
      setListError(`Couldn't load sessions: ${errorText(err)}`);
    }
  }, [client]);

  const checkHealth = useCallback(async () => {
    const reported = await client.info();
    setOnline(reported !== null);
    if (reported) setInfo(reported);
    return reported !== null;
  }, [client]);

  // Initial load, then a light heartbeat so the "Offline" state clears by
  // itself when Atlas comes back.
  useEffect(() => {
    void checkHealth().then((ok) => {
      if (ok) void refreshSessions();
    });
    const timer = setInterval(() => {
      void checkHealth().then((ok) => {
        if (ok) setListError((e) => (e ? null : e));
      });
    }, 15000);
    return () => clearInterval(timer);
  }, [checkHealth, refreshSessions]);

  // Keep the URL pointing at the open session so a reload (or a bookmark)
  // comes back to it.
  useEffect(() => {
    window.history.replaceState(null, "", `${window.location.pathname}${window.location.search}${HASH_PREFIX}${encodeURIComponent(activeId)}`);
  }, [activeId]);

  useEffect(() => {
    const onHash = () => {
      const id = readHash();
      if (id && id !== activeId && !busy) setActiveId(id);
    };
    window.addEventListener("hashchange", onHash);
    return () => window.removeEventListener("hashchange", onHash);
  }, [activeId, busy]);

  // Load the open session's transcript. The composer stays disabled until
  // this resolves, so a message can't be sent into a half-loaded session.
  useEffect(() => {
    let cancelled = false;
    setLoadingHistory(true);
    setTurns([]);
    void client.getHistory(activeId).then((messages) => {
      if (cancelled) return;
      setTurns((prev) => (prev.length > 0 ? prev : turnsFromHistory(messages, activeId)));
      setLoadingHistory(false);
    });
    return () => {
      cancelled = true;
    };
  }, [activeId, client]);

  const send = useCallback(
    async (text: string) => {
      const stamp = Date.now();
      const assistantId = `a${stamp}`;
      const sessionId = activeId;
      setNotice(null);
      setBusy(true);
      setTurns((t) => [...t, { id: `u${stamp}`, role: "user", text }, newAssistantTurn(assistantId)]);

      const update = (fn: (turn: Extract<Turn, { role: "assistant" }>) => Extract<Turn, { role: "assistant" }>) =>
        setTurns((all) => all.map((t) => (t.id === assistantId && t.role === "assistant" ? fn(t) : t)));

      try {
        await client.sendMessage({ sessionId, message: text, streamDeltas: true }, (event) => update((a) => applyEvent(a, event)), {
          onRetry: () => setNotice("Connection hiccup, retrying…"),
          onFallback: () => setNotice("Streaming isn't available right now; waiting for the full reply."),
        });
        setNotice(null);
      } catch (err) {
        update((a) => (a.status === "error" ? a : failTurn(a, errorText(err))));
        void checkHealth();
      } finally {
        setBusy(false);
        void refreshSessions();
      }
    },
    [activeId, client, checkHealth, refreshSessions]
  );

  const startNew = useCallback(() => {
    setActiveId(newSessionId());
    setNavOpen(false);
  }, []);

  const select = useCallback((id: string) => {
    setActiveId(id);
    setNavOpen(false);
  }, []);

  const confirmDelete = useCallback(async () => {
    const target = pendingDelete;
    if (!target) return;
    setPendingDelete(null);
    try {
      await client.deleteSession(target.id);
      setSessions((list) => list.filter((s) => s.id !== target.id));
      if (target.id === activeId) startNew();
    } catch (err) {
      setNotice(`Couldn't delete that session: ${errorText(err)}`);
    }
    void refreshSessions();
  }, [pendingDelete, client, activeId, startNew, refreshSessions]);

  const activeIsDraft = !sessions.some((s) => s.id === activeId);
  const activeSession = sessions.find((s) => s.id === activeId);
  const title = activeSession?.title || "New chat";
  const empty = turns.length === 0 && !loadingHistory;

  return (
    <div className="app">
      <Sidebar
        sessions={sessions}
        activeId={activeId}
        activeIsDraft={activeIsDraft}
        locked={busy}
        online={online}
        serverLabel={new URL(url, window.location.href).host}
        version={info?.version}
        listError={listError}
        open={navOpen}
        onSelect={select}
        onNew={startNew}
        onDelete={setPendingDelete}
        onClose={() => setNavOpen(false)}
      />
      {navOpen && <div className="scrim" onClick={() => setNavOpen(false)} />}

      <main className="main">
        <header className="header">
          <button type="button" className="icon-btn open-nav" aria-label="Open sidebar" onClick={() => setNavOpen(true)}>
            <svg width="18" height="18" viewBox="0 0 18 18" fill="none" aria-hidden="true">
              <path d="M3 5h12M3 9h12M3 13h12" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" />
            </svg>
          </button>
          <h1>{title}</h1>
          {info?.model && <span className="chip" title={info.backend ? `via ${info.backend}` : undefined}>{info.model}</span>}
        </header>

        {online === false && (
          <div className="banner banner-bad" role="alert">
            <span>Can't reach Atlas at {url}. Is it running?</span>
            <button type="button" className="btn btn-small" onClick={() => void checkHealth().then((ok) => { if (ok) void refreshSessions(); })}>
              Retry
            </button>
          </div>
        )}
        {notice && (
          <div className="banner" role="status">
            {notice}
          </div>
        )}

        {empty ? (
          <div className="empty">
            <div className="empty-inner">
              <svg width="56" height="56" viewBox="0 0 26 26" fill="none" aria-hidden="true">
                <circle cx="13" cy="13" r="11" stroke="var(--accent)" strokeWidth="1.6" />
                <path d="M13 2v22M2 13h22" stroke="var(--accent)" strokeWidth="1.2" opacity="0.5" />
                <circle cx="13" cy="13" r="3.5" fill="var(--accent)" />
              </svg>
              <h2>What are we working on?</h2>
              <p>Atlas runs on your machine. Ask a question, point it at a repository, or let the agent read files and use tools while you watch.</p>
              <div className="starters">
                {STARTERS.map((s) => (
                  <button key={s} type="button" className="btn" disabled={busy || online === false} onClick={() => void send(s)}>
                    {s}
                  </button>
                ))}
              </div>
            </div>
          </div>
        ) : (
          <Transcript turns={turns} />
        )}

        <Composer disabled={loadingHistory || online === false} busy={busy} onSend={(t) => void send(t)} />
      </main>

      {pendingDelete && (
        <ConfirmDialog
          title="Delete this session?"
          body={`“${pendingDelete.title || "Untitled chat"}” and all of its messages will be removed from disk. This cannot be undone.`}
          confirmLabel="Delete session"
          onConfirm={() => void confirmDelete()}
          onCancel={() => setPendingDelete(null)}
        />
      )}
    </div>
  );
}

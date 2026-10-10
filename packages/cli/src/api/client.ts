// Typed client for Atlas's REST API (GET /health, POST /chat, POST
// /chat/stream) - see the "API" section of the root README.md for the
// wire format this mirrors. This is the one place that knows Atlas's
// wire format; AtlasUI (React) is expected to share this same client
// eventually rather than reimplementing NDJSON parsing a second time.

export interface AtlasClientOptions {
  baseUrl: string;
}

export type AgentEvent =
  | { type: "iteration_start"; iteration: number; max_iterations: number }
  | { type: "thinking"; content: string }
  // Live text, only sent when the request asks for it (ChatRequest.streamDeltas):
  // just the NEW piece of the model's reasoning / reply, ahead of the usual
  // "thinking" / "assistant_thought" / "final" events that carry the complete text.
  | { type: "thinking_delta"; content: string }
  | { type: "content_delta"; content: string }
  | { type: "assistant_thought"; content: string }
  | { type: "tool_call"; name: string; arguments: unknown }
  | { type: "tool_result"; name: string; result: unknown }
  | { type: "final"; reply: string }
  // The turn was stopped (see AtlasClient.cancel). Ends the stream in place of
  // "final"; whatever the model had said by then is kept in the session.
  | { type: "cancelled" }
  | { type: "error"; message: string };

export interface ChatRequest {
  sessionId: string;
  message: string;
  workspace?: string;
  // Ask /chat/stream to also send the model's text as it is generated
  // (thinking_delta / content_delta events). Off by default so existing
  // clients see exactly the events they always did.
  streamDeltas?: boolean;
}

export interface ChatResponse {
  sessionId: string;
  reply: string;
  steps: AgentEvent[];
}

// A single raw message exactly as SessionManager/Agent persist it -
// {"role":"user"|"assistant"|"tool", "content":..., "tool_calls"?:...,
// "name"?:...}. Deliberately not narrowed further than this: GET
// /sessions/:id/history returns the complete, uncompacted history
// (including intermediate tool-call turns), not a simplified view, so a
// caller that wants to render it as a plain user/assistant transcript
// (see ui/App.tsx's turnsFromHistory) has to do its own filtering rather
// than relying on this type to have done it already.
export interface RawSessionMessage {
  role: string;
  content?: string;
  tool_calls?: unknown[];
  name?: string;
  // The model's reasoning for an assistant message, when it produced any.
  thinking?: string;
}

// One row of GET /sessions - what a session picker needs to render an entry.
export interface SessionSummary {
  id: string;
  // The session's first user message, shortened by the server. Empty when
  // the session has no user message.
  title: string;
  messageCount: number;
  // ISO 8601 UTC timestamp of the last write; empty if the server couldn't tell.
  updatedAt: string;
}

// What GET /health says about the server it is talking to. Only `status`
// is a stable contract; everything here is informational and any of it can
// be missing (older servers send just {"status":"ok"}).
export interface ServerInfo {
  version?: string;
  backend?: string;
  model?: string;
}

export class AtlasApiError extends Error {
  constructor(message: string, readonly cause?: unknown) {
    super(message);
    this.name = "AtlasApiError";
  }
}

// Thrown specifically when a request never got a response at all - the
// underlying fetch() itself rejected (DNS failure, connection refused,
// TCP reset before any bytes came back). This is a distinct subclass
// because it's the ONLY failure mode sendMessage() treats as safe to
// retry or fall back on: see its doc comment for why anything past this
// point (a non-2xx response, a stream that starts and then drops, a
// missing final/error line) must NOT be retried the same way.
export class AtlasConnectError extends AtlasApiError {
  constructor(message: string, cause?: unknown) {
    super(message, cause);
    this.name = "AtlasConnectError";
  }
}

function requestBody(request: ChatRequest): Record<string, unknown> {
  return {
    session_id: request.sessionId,
    message: request.message,
    workspace: request.workspace,
    ...(request.streamDeltas ? { stream_deltas: true } : {}),
  };
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

export interface SendMessageOptions {
  // Number of extra /chat/stream connection attempts after the first,
  // each with doubling backoff, before falling back to /chat. Default 2
  // (so up to 3 total /chat/stream attempts).
  maxRetries?: number;
  // Delay before the first retry, in ms; doubles each subsequent retry.
  // Default 500.
  retryDelayMs?: number;
  // Fired before each retry of /chat/stream.
  onRetry?: (info: { attempt: number; delayMs: number; error: Error }) => void;
  // Fired once, if every /chat/stream attempt failed to connect and
  // sendMessage is about to try the non-streaming /chat endpoint instead.
  onFallback?: (info: { reason: string }) => void;
}

export class AtlasClient {
  private readonly baseUrl: string;

  constructor(options: AtlasClientOptions) {
    // Strip a trailing slash so `${baseUrl}/health` never ends up
    // double-slashed regardless of how the caller passed --server.
    this.baseUrl = options.baseUrl.replace(/\/+$/, "");
  }

  // GET /sessions/:id/history - the complete, uncompacted history for a
  // session, exactly as persisted (see RawSessionMessage). Never throws:
  // a connect failure or a non-2xx response both just resolve to an
  // empty array, the same as a session that doesn't exist yet - callers
  // restoring a transcript on mount (ui/App.tsx) want "nothing to show"
  // either way, not a crash over a best-effort convenience fetch.
  async getHistory(sessionId: string): Promise<RawSessionMessage[]> {
    try {
      const res = await fetch(`${this.baseUrl}/sessions/${encodeURIComponent(sessionId)}/history`);
      if (!res.ok) return [];
      const body = (await res.json()) as { messages?: RawSessionMessage[] };
      return body.messages ?? [];
    } catch {
      return [];
    }
  }

  // GET /sessions - every session the server has persisted, most recently
  // updated first. Unlike getHistory() this throws on failure: a caller
  // rendering a session list wants to say "couldn't load sessions" rather
  // than show an empty list that looks like a fresh install. Servers older
  // than the /sessions endpoint answer 404, which surfaces as an
  // AtlasApiError too.
  async listSessions(): Promise<SessionSummary[]> {
    let res: Response;
    try {
      res = await fetch(`${this.baseUrl}/sessions`);
    } catch (err) {
      throw new AtlasConnectError(
        `couldn't reach Atlas server at ${this.baseUrl}: ${err instanceof Error ? err.message : String(err)}`,
        err
      );
    }
    if (!res.ok) {
      throw new AtlasApiError(`Atlas server returned HTTP ${res.status} listing sessions`);
    }
    const body = (await res.json().catch(() => undefined)) as
      | { sessions?: Array<{ id?: unknown; title?: unknown; message_count?: unknown; updated_at?: unknown }> }
      | undefined;
    const rows = Array.isArray(body?.sessions) ? body.sessions : [];
    const sessions: SessionSummary[] = [];
    for (const row of rows) {
      if (typeof row.id !== "string") continue;
      sessions.push({
        id: row.id,
        title: typeof row.title === "string" ? row.title : "",
        messageCount: typeof row.message_count === "number" ? row.message_count : 0,
        updatedAt: typeof row.updated_at === "string" ? row.updated_at : "",
      });
    }
    return sessions;
  }

  // DELETE /sessions/:id - permanently removes a session's history and
  // compaction summary, both server-side (SessionManager::resetSession)
  // and, by the caller, from the local picker registry (see
  // config/sessions.ts's removeSession). Throws AtlasConnectError if the
  // request can't even reach the server - unlike getHistory(), a failed
  // delete is NOT something a caller should silently swallow, since the
  // caller (ui/SessionPicker.tsx) needs to know whether it's actually
  // safe to drop the entry from its own local list.
  async deleteSession(sessionId: string): Promise<void> {
    let res: Response;
    try {
      res = await fetch(`${this.baseUrl}/sessions/${encodeURIComponent(sessionId)}`, {
        method: "DELETE",
      });
    } catch (err) {
      throw new AtlasConnectError(
        `couldn't reach Atlas server at ${this.baseUrl}: ${err instanceof Error ? err.message : String(err)}`,
        err
      );
    }
    if (!res.ok) {
      throw new AtlasApiError(`Atlas server returned HTTP ${res.status} deleting session ${sessionId}`);
    }
  }

  // Asks Atlas to stop the turn currently running for `sessionId` (the Stop
  // button). Resolves to whether there was a running turn to stop. This only
  // asks: the turn's own stream is what reports that it really ended, with a
  // {"type":"cancelled"} event, so a caller should keep waiting on that
  // stream (and not send another message to this session) until it does.
  // Throws AtlasConnectError if the server can't be reached.
  async cancel(sessionId: string): Promise<boolean> {
    let res: Response;
    try {
      res = await fetch(`${this.baseUrl}/sessions/${encodeURIComponent(sessionId)}/cancel`, {
        method: "POST",
        // The server reads a POST body, so send a (trivial) one.
        headers: { "Content-Type": "application/json" },
        body: "{}",
      });
    } catch (err) {
      throw new AtlasConnectError(
        `couldn't reach Atlas server at ${this.baseUrl}: ${err instanceof Error ? err.message : String(err)}`,
        err
      );
    }
    if (!res.ok) {
      throw new AtlasApiError(`Atlas server returned HTTP ${res.status} stopping session ${sessionId}`);
    }
    const body = (await res.json().catch(() => ({}))) as { cancelled?: unknown };
    return body.cancelled === true;
  }

  // Liveness check against GET /health. Never throws - a request that
  // can't even connect is just as "not ok" as one that connects and
  // reports something other than {"status":"ok"}, and callers (the UI)
  // want a single boolean to render a banner from either way.
  async health(): Promise<boolean> {
    try {
      const res = await fetch(`${this.baseUrl}/health`);
      if (!res.ok) return false;
      const body = (await res.json()) as { status?: string };
      return body.status === "ok";
    } catch {
      return false;
    }
  }

  // Like health(), but also returns what the server reports about itself.
  // Resolves to null when the server is unreachable or not ok - never
  // throws - and to an object (possibly empty) when it is up.
  async info(): Promise<ServerInfo | null> {
    try {
      const res = await fetch(`${this.baseUrl}/health`);
      if (!res.ok) return null;
      const body = (await res.json()) as { status?: string; version?: unknown; backend?: unknown; model?: unknown };
      if (body.status !== "ok") return null;
      const info: ServerInfo = {};
      if (typeof body.version === "string") info.version = body.version;
      if (typeof body.backend === "string") info.backend = body.backend;
      if (typeof body.model === "string") info.model = body.model;
      return info;
    } catch {
      return null;
    }
  }

  // POST /chat - blocks until the whole ReAct loop finishes, then
  // returns the final reply plus the full step trace. Prefer
  // chatStream() for anything interactive; this exists mainly so the
  // wire format has one tested client-side implementation even for
  // callers that don't want to stream, and it doubles as sendMessage()'s
  // fallback when /chat/stream can't be reached at all.
  async chat(request: ChatRequest): Promise<ChatResponse> {
    let res: Response;
    try {
      res = await fetch(`${this.baseUrl}/chat`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(requestBody(request)),
      });
    } catch (err) {
      throw new AtlasConnectError(
        `couldn't reach Atlas server at ${this.baseUrl}: ${err instanceof Error ? err.message : String(err)}`,
        err
      );
    }

    const body = await res.json().catch(() => undefined);

    if (!res.ok) {
      const serverMessage =
        body && typeof body === "object" && "error" in body ? String((body as { error: unknown }).error) : undefined;
      throw new AtlasApiError(serverMessage ?? `Atlas server returned HTTP ${res.status}`);
    }

    const parsed = body as { session_id: string; reply: string; steps?: AgentEvent[] };
    return {
      sessionId: parsed.session_id,
      reply: parsed.reply,
      steps: parsed.steps ?? [],
    };
  }

  // Streams one turn's events as they arrive over POST /chat/stream
  // (NDJSON: one JSON object per line, ending in a "final" or "error"
  // line). `onEvent` fires once per parsed line, in order. Resolves with
  // the "final" event's reply once the stream ends normally; rejects if
  // the stream ends on an "error" line, the connection drops mid-stream,
  // or the server closes the response without ever sending a
  // "final"/"error" line at all - a protocol violation on Atlas's side
  // that a caller shouldn't have to detect for itself.
  //
  // Throws AtlasConnectError specifically when the request never got a
  // response (fetch() itself rejected); every other failure throws the
  // plain AtlasApiError base class. Callers that want retry/fallback
  // behavior should use sendMessage() instead of calling this directly.
  async chatStream(request: ChatRequest, onEvent: (event: AgentEvent) => void): Promise<string> {
    let res: Response;
    try {
      res = await fetch(`${this.baseUrl}/chat/stream`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(requestBody(request)),
      });
    } catch (err) {
      throw new AtlasConnectError(
        `couldn't reach Atlas server at ${this.baseUrl}: ${err instanceof Error ? err.message : String(err)}`,
        err
      );
    }

    if (!res.ok || !res.body) {
      const text = await res.text().catch(() => "");
      throw new AtlasApiError(`Atlas server returned HTTP ${res.status}${text ? `: ${text}` : ""}`);
    }

    const reader = res.body.getReader();
    const decoder = new TextDecoder();
    let buffer = "";
    let finalReply: string | undefined;
    let streamError: string | undefined;
    let wasCancelled = false;

    try {
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        // { stream: true } is load-bearing: a multi-byte UTF-8 character
        // can straddle two chunks, and without it the decoder would emit
        // a replacement character for the half it sees in each call.
        buffer += decoder.decode(value, { stream: true });

        let newlineIndex = buffer.indexOf("\n");
        while (newlineIndex >= 0) {
          const line = buffer.slice(0, newlineIndex).trim();
          buffer = buffer.slice(newlineIndex + 1);

          if (line) {
            let event: AgentEvent;
            try {
              event = JSON.parse(line) as AgentEvent;
            } catch (parseError) {
              throw new AtlasApiError(`malformed NDJSON line from Atlas server: ${line}`, parseError);
            }

            onEvent(event);
            if (event.type === "final") finalReply = event.reply;
            if (event.type === "error") streamError = event.message;
            if (event.type === "cancelled") wasCancelled = true;
          }

          newlineIndex = buffer.indexOf("\n");
        }
      }
    } finally {
      reader.releaseLock();
    }

    if (streamError !== undefined) {
      throw new AtlasApiError(streamError);
    }
    if (finalReply === undefined && wasCancelled) {
      return "";
    }
    if (finalReply === undefined) {
      throw new AtlasApiError("Atlas server closed the stream without a final or error event");
    }
    return finalReply;
  }

  // The method interactive callers (the CLI's App component) should use
  // for a turn instead of calling chatStream() directly. It adds two
  // things on top of chatStream(), both scoped narrowly on purpose:
  //
  // 1. Retry with backoff, but ONLY when /chat/stream never got a
  //    response at all (AtlasConnectError - see its doc comment). Once a
  //    response has started arriving, Atlas's agent loop has almost
  //    certainly already appended the user's message to the session and
  //    may already have executed tools with real side effects (a git
  //    commit, a file write). Atlas's API has no idempotency keys, so
  //    resending the same message after that point risks duplicating
  //    that work rather than safely "retrying" it. A connect-level
  //    failure - the request never left, or never got any reply - is the
  //    one case where resending is a reasonable bet, and even that is a
  //    best-effort heuristic rather than a guarantee.
  //
  // 2. Falling back to the non-streaming /chat once, but only after every
  //    /chat/stream attempt failed at that same connect stage. The
  //    fallback's steps/final are replayed through onEvent synchronously
  //    (all at once, since /chat doesn't stream) so callers see the same
  //    event shape either way.
  //
  // A mid-stream failure (bad status, dropped connection after some
  // events arrived, missing final/error line) is rethrown immediately -
  // no retry, no fallback - since by that point a duplicate attempt is
  // the more dangerous choice.
  async sendMessage(
    request: ChatRequest,
    onEvent: (event: AgentEvent) => void,
    options: SendMessageOptions = {}
  ): Promise<string> {
    const maxRetries = options.maxRetries ?? 2;
    const baseDelayMs = options.retryDelayMs ?? 500;

    let lastError: unknown;
    for (let attempt = 0; attempt <= maxRetries; attempt++) {
      try {
        return await this.chatStream(request, onEvent);
      } catch (err) {
        lastError = err;
        if (!(err instanceof AtlasConnectError)) {
          throw err;
        }
        if (attempt < maxRetries) {
          const delayMs = baseDelayMs * 2 ** attempt;
          options.onRetry?.({ attempt: attempt + 1, delayMs, error: err });
          await sleep(delayMs);
        }
      }
    }

    options.onFallback?.({
      reason: lastError instanceof Error ? lastError.message : String(lastError),
    });

    const response = await this.chat(request);
    for (const event of response.steps) onEvent(event);
    onEvent({ type: "final", reply: response.reply });
    return response.reply;
  }
}

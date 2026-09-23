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
  | { type: "assistant_thought"; content: string }
  | { type: "tool_call"; name: string; arguments: unknown }
  | { type: "tool_result"; name: string; result: unknown }
  | { type: "final"; reply: string }
  | { type: "error"; message: string };

export interface ChatRequest {
  sessionId: string;
  message: string;
  workspace?: string;
}

export interface ChatResponse {
  sessionId: string;
  reply: string;
  steps: AgentEvent[];
}

export class AtlasApiError extends Error {
  constructor(message: string, readonly cause?: unknown) {
    super(message);
    this.name = "AtlasApiError";
  }
}

function requestBody(request: ChatRequest): Record<string, unknown> {
  return {
    session_id: request.sessionId,
    message: request.message,
    workspace: request.workspace,
  };
}

export class AtlasClient {
  private readonly baseUrl: string;

  constructor(options: AtlasClientOptions) {
    // Strip a trailing slash so `${baseUrl}/health` never ends up
    // double-slashed regardless of how the caller passed --server.
    this.baseUrl = options.baseUrl.replace(/\/+$/, "");
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

  // POST /chat - blocks until the whole ReAct loop finishes, then
  // returns the final reply plus the full step trace. Prefer
  // chatStream() for anything interactive; this exists mainly so the
  // wire format has one tested client-side implementation even for
  // callers that don't want to stream.
  async chat(request: ChatRequest): Promise<ChatResponse> {
    const res = await fetch(`${this.baseUrl}/chat`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(requestBody(request)),
    });

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
  async chatStream(request: ChatRequest, onEvent: (event: AgentEvent) => void): Promise<string> {
    const res = await fetch(`${this.baseUrl}/chat/stream`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(requestBody(request)),
    });

    if (!res.ok || !res.body) {
      const text = await res.text().catch(() => "");
      throw new AtlasApiError(`Atlas server returned HTTP ${res.status}${text ? `: ${text}` : ""}`);
    }

    const reader = res.body.getReader();
    const decoder = new TextDecoder();
    let buffer = "";
    let finalReply: string | undefined;
    let streamError: string | undefined;

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
    if (finalReply === undefined) {
      throw new AtlasApiError("Atlas server closed the stream without a final or error event");
    }
    return finalReply;
  }
}

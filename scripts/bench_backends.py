#!/usr/bin/env python3
"""Benchmark Ollama against an OpenAI-compatible server (llama.cpp's llama-server).

Standard library only, so it runs unchanged on Windows, Linux and macOS:

    python scripts/bench_backends.py \
        --ollama-model qwen3:14b \
        --llama-url http://127.0.0.1:8081

Point it at one server or both (leave out --ollama-model to skip Ollama, or
--llama-url to skip llama-server). On a single GPU run them one at a time
(a 14B model will not fit twice in 16 GB), saving each run with --output, then
merge the saved files into one table with --combine. For each backend it sends the same fixed
prompts with temperature 0 and the same token cap, streams the reply, and
reports time-to-first-token plus prefill and decode speed, as the median of
--runs runs after one untimed warm-up.

See docs/specs/backend-benchmark.md for how to set up an apples-to-apples
comparison (same GGUF file, same context size, same GPU offload) - the numbers
are only meaningful if the two servers are configured alike.
"""

from __future__ import annotations

import argparse
import json
import platform
import statistics
import sys
import time
import uuid
import urllib.error
import urllib.request
from datetime import datetime, timezone

SHORT_PROMPT = "Explain in two or three sentences what a mutex is and why it is needed."

CODE_PROMPT = """Review this C++ function and list every bug you can find, briefly.

```cpp
#include <vector>
#include <string>
#include <map>

std::map<std::string, int> countWords(const std::vector<std::string>& lines) {
    std::map<std::string, int> counts;
    for (int i = 0; i <= lines.size(); ++i) {
        std::string word;
        for (char c : lines[i]) {
            if (c == ' ') {
                counts[word]++;
                word = "";
            } else {
                word += c;
            }
        }
    }
    return counts;
}

int* firstPositive(std::vector<int>& values) {
    for (auto v : values) {
        if (v > 0) return &v;
    }
    return nullptr;
}
```"""

_FILLER = (
    "The quick brown fox jumps over the lazy dog while the build pipeline compiles "
    "another translation unit and the test runner waits for the linker to finish. "
)
LONG_PROMPT = (
    "Below is a long, repetitive log excerpt. After reading it, reply with exactly "
    "one sentence saying what the text is about.\n\n" + _FILLER * 90
)

PROMPTS = [
    ("short", SHORT_PROMPT),
    ("code-review", CODE_PROMPT),
    ("long-context", LONG_PROMPT),
]


def post_stream(url: str, payload: dict, headers: dict | None = None):
    """POST JSON and yield raw response lines as they arrive."""
    request = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json", **(headers or {})},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=900) as response:
        for raw in response:
            line = raw.decode("utf-8", errors="replace").strip()
            if line:
                yield line


def get_json(url: str):
    try:
        with urllib.request.urlopen(url, timeout=10) as response:
            return json.loads(response.read().decode("utf-8"))
    except Exception:
        return None


def run_ollama(args, prompt: str) -> dict:
    payload = {
        "model": args.ollama_model,
        "messages": [{"role": "user", "content": prompt}],
        "stream": True,
        "options": {
            "temperature": 0,
            "seed": 1,
            "num_predict": args.max_tokens,
            "num_ctx": args.ctx,
        },
    }
    if args.no_think:
        payload["think"] = False

    started = time.perf_counter()
    first = None
    final = {}
    chunks = 0
    for line in post_stream(args.ollama_url.rstrip("/") + "/api/chat", payload):
        data = json.loads(line)
        message = data.get("message") or {}
        if first is None and (message.get("content") or message.get("thinking")):
            first = time.perf_counter() - started
        if message.get("content") or message.get("thinking"):
            chunks += 1
        if data.get("done"):
            final = data
    total = time.perf_counter() - started

    eval_count = final.get("eval_count", chunks)
    eval_ns = final.get("eval_duration") or 0
    prompt_count = final.get("prompt_eval_count", 0)
    prompt_ns = final.get("prompt_eval_duration") or 0
    return {
        "ttft_s": first if first is not None else total,
        "total_s": total,
        "prompt_tokens": prompt_count,
        "completion_tokens": eval_count,
        "prefill_tps": (prompt_count / (prompt_ns / 1e9)) if prompt_ns else None,
        "decode_tps_server": (eval_count / (eval_ns / 1e9)) if eval_ns else None,
    }


def run_openai(args, prompt: str) -> dict:
    payload = {
        "model": args.llama_model,
        "messages": [{"role": "user", "content": prompt}],
        "stream": True,
        "stream_options": {"include_usage": True},
        "temperature": 0,
        "seed": 1,
        "max_tokens": args.max_tokens,
    }
    if args.no_think:
        payload["chat_template_kwargs"] = {"enable_thinking": False}

    base = args.llama_url.rstrip("/")
    if base.endswith("/v1"):
        base = base[: -len("/v1")]
    headers = {"Authorization": f"Bearer {args.api_key}"} if args.api_key else None

    started = time.perf_counter()
    first = None
    usage = {}
    timings = {}
    chunks = 0
    for line in post_stream(base + "/v1/chat/completions", payload, headers):
        if not line.startswith("data:"):
            continue
        body = line[len("data:"):].strip()
        if body == "[DONE]":
            break
        data = json.loads(body)
        if data.get("usage"):
            usage = data["usage"]
        if data.get("timings"):
            timings = data["timings"]
        for choice in data.get("choices") or []:
            delta = choice.get("delta") or {}
            if delta.get("content") or delta.get("reasoning_content") or delta.get("reasoning"):
                chunks += 1
                if first is None:
                    first = time.perf_counter() - started
    total = time.perf_counter() - started

    completion = usage.get("completion_tokens") or timings.get("predicted_n") or chunks
    prompt_count = usage.get("prompt_tokens") or timings.get("prompt_n") or 0
    return {
        "ttft_s": first if first is not None else total,
        "total_s": total,
        "prompt_tokens": prompt_count,
        "completion_tokens": completion,
        "prefill_tps": timings.get("prompt_per_second"),
        "decode_tps_server": timings.get("predicted_per_second"),
    }


def with_wall_decode(run: dict) -> dict:
    """Decode speed from the client's own clock, as a cross-check on server-reported numbers."""
    decode_s = run["total_s"] - run["ttft_s"]
    tokens = run["completion_tokens"]
    run["decode_tps_wall"] = (tokens - 1) / decode_s if decode_s > 0 and tokens > 1 else None
    return run


def median(values):
    values = [v for v in values if v is not None]
    return statistics.median(values) if values else None


def fmt(value, digits=1):
    return "-" if value is None else f"{value:.{digits}f}"


def unique_prompt(prompt: str, args) -> str:
    """Prefix a throwaway id so a server's prompt cache can't reuse an earlier run's work.

    Both Ollama and llama-server remember the previous prompt and skip
    re-processing a shared prefix. With identical prompts every run after the
    first would show near-zero prefill time, and the median would report that
    instead of real prompt-processing speed. The id goes at the very start so
    nothing after it can match. Use --reuse-cache to measure the cached case.
    """
    if args.reuse_cache:
        return prompt
    return f"[request {uuid.uuid4().hex[:8]}]\n{prompt}"


def benchmark(label: str, runner, args) -> list[dict]:
    rows = []
    print(f"\n== {label} ==", file=sys.stderr)
    print("warm-up (untimed)...", file=sys.stderr)
    runner(args, SHORT_PROMPT)
    for name, prompt in PROMPTS:
        runs = []
        for i in range(args.runs):
            result = with_wall_decode(runner(args, unique_prompt(prompt, args)))
            runs.append(result)
            print(
                f"  {name} run {i + 1}/{args.runs}: ttft {result['ttft_s']:.2f}s, "
                f"{result['completion_tokens']} tokens, "
                f"decode {fmt(result['decode_tps_server'] or result['decode_tps_wall'])} tok/s",
                file=sys.stderr,
            )
        rows.append(
            {
                "backend": label,
                "prompt": name,
                "prompt_tokens": median([r["prompt_tokens"] for r in runs]),
                "completion_tokens": median([r["completion_tokens"] for r in runs]),
                "ttft_s": median([r["ttft_s"] for r in runs]),
                "prefill_tps": median([r["prefill_tps"] for r in runs]),
                "decode_tps_server": median([r["decode_tps_server"] for r in runs]),
                "decode_tps_wall": median([r["decode_tps_wall"] for r in runs]),
                "runs": runs,
            }
        )
    return rows


def markdown_table(rows: list[dict]) -> str:
    lines = [
        "| prompt | backend | prompt tok | gen tok | TTFT (s) | prefill tok/s | decode tok/s (server) | decode tok/s (wall) |",
        "|---|---|---:|---:|---:|---:|---:|---:|",
    ]
    for r in sorted(rows, key=lambda r: ([p for p, _ in PROMPTS].index(r["prompt"]), r["backend"])):
        lines.append(
            f"| {r['prompt']} | {r['backend']} | {fmt(r['prompt_tokens'], 0)} | {fmt(r['completion_tokens'], 0)} "
            f"| {fmt(r['ttft_s'], 2)} | {fmt(r['prefill_tps'])} | {fmt(r['decode_tps_server'])} "
            f"| {fmt(r['decode_tps_wall'])} |"
        )
    return "\n".join(lines)


def parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--ollama-url", default="http://127.0.0.1:11434")
    p.add_argument("--ollama-model", help="Ollama model tag, e.g. qwen3:14b (omit to skip Ollama)")
    p.add_argument("--llama-url", help="OpenAI-compatible server base URL, e.g. http://127.0.0.1:8081 (omit to skip)")
    p.add_argument("--llama-model", default="model", help="model name to send (llama-server ignores it; default: model)")
    p.add_argument("--api-key", default="", help="bearer token for the OpenAI-compatible server, if it needs one")
    p.add_argument("--runs", type=int, default=3, help="timed runs per prompt (default 3)")
    p.add_argument("--max-tokens", type=int, default=256, help="generation cap per run (default 256)")
    p.add_argument("--ctx", type=int, default=8192, help="context size sent to Ollama; start llama-server with the same -c")
    p.add_argument("--reuse-cache", action="store_true",
                   help="send identical prompts every run so the server's prompt cache is used (default: unique prompts, "
                        "which measures real prefill speed)")
    p.add_argument("--no-think", action="store_true", help="ask both servers to disable reasoning output (best effort)")
    p.add_argument("--output", help="also write raw results to this JSON file")
    p.add_argument("--combine", nargs="+", metavar="FILE",
                   help="don't benchmark; print one table from result files previously saved with --output")
    args = p.parse_args(argv)
    if not args.combine and not args.ollama_model and not args.llama_url:
        p.error("give at least one of --ollama-model or --llama-url (or --combine FILE...)")
    return args


def combine(paths: list[str]) -> int:
    rows: list[dict] = []
    for path in paths:
        with open(path, encoding="utf-8") as fh:
            saved = json.load(fh)
        rows += saved["results"]
        env = saved.get("environment", {})
        used = env.get("args", {})
        print(f"{path}: {env.get('timestamp', '?')}, runs={used.get('runs')}, max_tokens={used.get('max_tokens')}, "
              f"ctx={used.get('ctx')}, no_think={used.get('no_think')}", file=sys.stderr)
    if not rows:
        print("error: no results found in the given files", file=sys.stderr)
        return 2
    print(markdown_table(rows))
    return 0


def main(argv=None) -> int:
    args = parse_args(argv)
    if args.combine:
        return combine(args.combine)
    rows: list[dict] = []
    environment = {
        "timestamp": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "python": platform.python_version(),
        "platform": platform.platform(),
        "args": {k: v for k, v in vars(args).items() if k != "api_key"},
    }

    if args.ollama_model:
        environment["ollama_version"] = (get_json(args.ollama_url.rstrip("/") + "/api/version") or {}).get("version")
        try:
            rows += benchmark(f"ollama ({args.ollama_model})", run_ollama, args)
        except (urllib.error.URLError, OSError) as exc:
            print(f"error: could not benchmark Ollama at {args.ollama_url}: {exc}", file=sys.stderr)

    if args.llama_url:
        environment["llama_props"] = get_json(args.llama_url.rstrip("/").removesuffix("/v1") + "/props")
        if isinstance(environment["llama_props"], dict):
            # Keep just the build identity; the full props payload includes the chat template.
            environment["llama_props"] = {
                k: environment["llama_props"].get(k) for k in ("build_info", "model_path") if k in environment["llama_props"]
            }
        try:
            rows += benchmark("openai-compat (llama-server)", run_openai, args)
        except (urllib.error.URLError, OSError) as exc:
            print(f"error: could not benchmark {args.llama_url}: {exc}", file=sys.stderr)

    if not rows:
        print("error: no backend produced results", file=sys.stderr)
        return 2

    print()
    print(markdown_table(rows))
    print(
        f"\n_median of {args.runs} runs, temperature 0, max {args.max_tokens} tokens, ctx {args.ctx}"
        f"{', thinking disabled' if args.no_think else ''}"
        f"{', prompt cache allowed' if args.reuse_cache else ', unique prompts (cold prefill)'}_"
    )

    if args.output:
        with open(args.output, "w", encoding="utf-8") as fh:
            json.dump({"environment": environment, "results": rows}, fh, indent=2)
        print(f"raw results written to {args.output}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())

# Running Atlas + Ollama on an AMD GPU (ROCm)

Status: guidance / spec, not code. Written for a Radeon RX 9060 XT (16GB,
RDNA4) but the sizing advice generalizes to any 12-24GB AMD card.

## Why this needs its own doc

Atlas itself doesn't talk to the GPU at all - `LLMClient` just speaks HTTP
to Ollama's `/api/chat` (see `src/agent/LLMClient.cpp`). All of the GPU
setup and model-fit tradeoffs below live entirely on the Ollama side of
that boundary. This doc exists so that boundary is understood, and so a
15-minute ROCm rabbit hole doesn't get mistaken for an Atlas bug.

## ROCm / driver setup

- RDNA4 (RX 9070/9070 XT/9060 XT) maps to LLVM target **gfx1200** or
  **gfx1201** depending on the exact card and which ROCm/Ollama version
  you're on - sources disagree on which of the two the 9060 XT is, so
  don't hardcode either into scripts. Check your actual Ollama server log
  on startup; it prints the detected GPU and target.
- Recent Ollama releases (ROCm 7.x-based) support RDNA4 on Windows
  directly - no more manually swapping in newer `rocblas`/`hipBLASLt` DLLs
  from a separate ROCm-for-Windows install, which older guides describe.
  If you hit "GPU not detected" or "0 VRAM" on Linux/WSL, that's a known
  class of issue for very new cards; check you're on the latest Ollama
  release before assuming it's an Atlas or driver problem.
- If Ollama doesn't recognize the card at all, `HSA_OVERRIDE_GFX_VERSION`
  (format `x.y.z`, e.g. `12.0.0` for gfx1200) forces it to treat the GPU as
  a specific target. Last resort, not a default to set proactively.
- `ROCR_VISIBLE_DEVICES` restricts which GPU(s) Ollama uses if you ever
  have more than one in the machine.

Check `https://docs.ollama.com/gpu` for the current supported-target list
before troubleshooting further - this is exactly the kind of thing that
changes between Ollama releases.

## Fitting a model in 16GB of VRAM

Rule of thumb for a Q4_K_M-quantized model: roughly 0.5-0.6GB of VRAM per
billion parameters for the weights, plus KV cache that grows with context
length and is bigger for longer conversations (Atlas's Agent loop
re-sends the full session history every iteration - see
`Agent::chat` in `src/agent/Agent.cpp` - so a long self-improvement
session's context keeps growing turn over turn).

On a 16GB card:

- **qwen2.5-coder:14b** (the README's default) at Q4_K_M is the sweet
  spot: comfortably fits with several thousand tokens of context to
  spare, and is a strong tool-calling model, which matters more for Atlas
  than raw parameter count - `Agent::extractToolCalls` only falls back to
  scraping a fenced ` ```json ` block when a model doesn't reliably emit
  native `tool_calls`, and that fallback is strictly worse (see the
  known-limitations note in the README about `qwen3:4b`).
- Going up to a 32b-class coder model at Q4_K_M will not fit alongside
  meaningful context on 16GB - you'd be trading away the context budget
  Atlas's whole "Librarian" search_symbol/read_file design exists to
  economize. Not recommended on this card.
- Going down to a 7-8b model buys more context headroom and faster
  responses, at the cost of tool-calling reliability - worth trying if
  the self-improvement loop's iteration latency (README's `--model` flag)
  matters more than getting native tool_calls every time, but verify
  tool-calling behavior with a throwaway `default`-workspace chat before
  trusting it in the `self` workspace.

## Practical Atlas flags

```bash
./atlas \
  --model=qwen2.5-coder:14b \
  --ollama-host=127.0.0.1 \
  --ollama-port=11434
```

`LLMClient` sets a 300-second read timeout specifically because local
inference (especially anything spilling out of VRAM onto CPU) can be
slow - if you see `LLMClient: Ollama returned HTTP ...` timeouts on a
GPU-accelerated model that should easily run in time, that's a sign the
model didn't actually load onto the GPU (check `ollama ps` for the
`PROCESSOR` column - it should say `100% GPU`, not a CPU/GPU split).

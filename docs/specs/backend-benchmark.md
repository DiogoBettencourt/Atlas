# Backend benchmark: Ollama vs. llama-server

Part of the own-inference-engine investigation (#47). This is the **decision
gate** between Phase 1 (an OpenAI-compatible backend, #49) and Phase 2 (an
embedded llama.cpp backend, #50): before building anything deeper, measure
whether running a ROCm-tuned llama.cpp directly is meaningfully better than
Ollama on the hardware we actually use.

"OpenAI-compatible" only names the request format (`/v1/chat/completions`).
No OpenAI service is involved - `llama-server` is llama.cpp's own HTTP server,
running on your machine.

**Status: measured (2026-10-10, RX 9060 XT).** Results and the Phase 2
recommendation are at the bottom. Tool calling against a real llama-server was
not tested (see the end of this doc).

## What gets compared

| | Ollama | llama-server |
|---|---|---|
| API | `/api/chat` | `/v1/chat/completions` |
| Atlas backend | `--backend=ollama` | `--backend=openai` |
| Engine | llama.cpp bundled inside Ollama | llama.cpp built/downloaded by you |

Both run the same model weights, so any difference comes from the engine and
its settings, not the model.

## Keep it apples-to-apples

The comparison is only meaningful if both servers are configured alike:

- **Same GGUF file.** Don't let each server download its own copy. Ollama
  already has one; point llama-server at it (step 1).
- **Same context size.** The script sends `num_ctx` to Ollama
  (`--ctx`, default 8192); start llama-server with the same `-c`.
- **Same GPU offload.** Both should be 100% on the GPU. Check Ollama with
  `ollama ps` (the PROCESSOR column should say `100% GPU`) and llama-server's
  startup log (it reports how many layers were offloaded).
- **Same sampling and length.** The script uses temperature 0, a fixed seed and
  the same token cap for both.
- **One model on the GPU at a time.** A 14B model plus its context uses most
  of a 16 GB card, so Ollama and llama-server cannot both hold it at once
  without spilling layers to the CPU, which would wreck the numbers. Run the
  two benchmarks one after the other and unload the first (step 4). Also close
  anything else that uses the GPU heavily.
- **A warmed-up run, with a cold prompt each time:** the script does one
  untimed warm-up per backend and reports the median of the timed runs. Each
  timed run gets a throwaway id at the start of its prompt, because both
  servers cache the previous prompt and skip re-processing a shared prefix.
  Without that, repeated identical prompts show near-zero prefill after the
  first run (a first attempt reported ~69,000 tok/s that way) and the median
  describes the cache, not the engine. `--reuse-cache` turns this off if you
  want to measure the cached case separately.

## Step 1: find the exact GGUF Ollama uses

```
ollama show qwen3:14b --modelfile
```

The `FROM` line is the path to the model blob, e.g.
`C:\Users\<you>\.ollama\models\blobs\sha256-...`. That file is a plain GGUF;
pass it to llama-server with `-m`. (Use the same model tag you run Atlas with
today - the baseline is `qwen3:14b` at about 30 tokens/s, 100% GPU.)

## Step 2: get a llama-server that uses the GPU

Either:

- **A prebuilt release.** Check the
  [llama.cpp releases](https://github.com/ggml-org/llama.cpp/releases) for a
  Windows build with HIP (AMD ROCm) or Vulkan support. This is the quickest
  way to try it. Note which build you used, since it goes in the results.
- **Build from source with HIP.** Install the AMD HIP SDK for Windows (the RX
  9060 XT is listed as supported there) plus Visual Studio Build Tools and
  Ninja. The RX 9060 XT's GPU target is `gfx1200` (the 9070 series is
  `gfx1201`). From an x64 Native Tools prompt, following llama.cpp's
  [build guide](https://github.com/ggml-org/llama.cpp/blob/master/docs/build.md):

  ```
  set PATH=%HIP_PATH%\bin;%PATH%
  cmake -S . -B build -G Ninja -DGPU_TARGETS=gfx1200 -DGGML_HIP=ON ^
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
  cmake --build build
  ```

Vulkan is a separate, simpler backend (`-DGGML_VULKAN=ON`) and worth including
as a third data point if HIP gives trouble.

**Check the GPU is actually used before benchmarking.** Run
`llama-server --list-devices`. It must list your card (for example
`ROCm0: AMD Radeon RX 9060 XT (16304 MiB ...)`); `(none)` means the GPU backend
did not load and the server will silently run on the CPU, at about 2 tokens/s
here instead of about 30.

Two things went wrong on the first attempt and are worth knowing:

- **The official `llama-bXXXX-bin-win-rocm-*-x64.zip` does not bundle AMD's
  math libraries** (`hipblas.dll`, `rocblas.dll`). They are expected to come from
  a separately installed ROCm package on `PATH`. Without them `ggml-hip.dll`
  fails to load and llama.cpp quietly falls back to the CPU. `--list-devices`
  printing `(none)` is the symptom.
- **What worked:** the self-contained
  [lemonade-sdk/llamacpp-rocm](https://github.com/lemonade-sdk/llamacpp-rocm)
  Windows build for the `gfx120X` family (covers the RX 9060 XT), which bundles
  ROCm. No separate install was needed.

## Step 3: start llama-server

```
llama-server -m <path-to-gguf> --port 8081 -ngl 99 -c 8192 --flash-attn on --jinja
```

- `--port 8081`: llama-server defaults to 8080, which is Atlas's own default
  port. Keep them apart.
- `-ngl 99`: offload every layer to the GPU.
- `-c 8192`: must match the script's `--ctx`.
- `--jinja`: needed for tool calling, which Atlas relies on. (Recent builds
  enable it by default; passing it is harmless.) Tool-calling quality depends
  on the model's chat template.
- Avoid aggressive KV-cache quantization such as `-ctk q4_0` while evaluating
  Atlas: llama.cpp's own docs warn it can substantially degrade tool calling.

## Step 4: run the benchmark

Needs Python 3.9+ (standard library only). Run it from the repo root, one
backend at a time, so only one copy of the model is in GPU memory:

```
# 1) Ollama. llama-server must NOT be running yet.
python scripts/bench_backends.py --ollama-model qwen3:14b --output ollama.json

# 2) Unload the model from Ollama (otherwise it stays resident for a few minutes)
ollama stop qwen3:14b

# 3) Start llama-server (step 3) in another terminal, wait for "listening", then:
python scripts/bench_backends.py --llama-url http://127.0.0.1:8081 --output llama.json

# 4) Merge both runs into one table
python scripts/bench_backends.py --combine ollama.json llama.json
```

Useful options: `--runs` (default 3), `--max-tokens` (default 256), `--ctx`
(default 8192), `--no-think` (asks the server to switch off reasoning output,
best effort; reasoning models otherwise spend part of the token budget
thinking - use it for both runs or neither), and `--api-key` if the server
needs one.

Each run prints a markdown table; the final `--combine` step prints the
side-by-side table to paste into the results section below. The JSON files hold
every individual run and the server versions.

**Reading the columns**

- *TTFT*: wall-clock seconds from sending the request to the first streamed
  token. Dominated by prompt processing for the long prompt.
- *prefill tok/s*: prompt-processing speed, as reported by the server. Only
  trust it for the long prompt: with a ~30-token prompt it is dominated by
  fixed overhead and is not comparable.
- *decode tok/s (server)*: generation speed as the server measures it. This is
  the headline number.
- *decode tok/s (wall)*: the same thing measured by the client's clock, as a
  cross-check. It should land close to the server figure.

## Step 5: try it in Atlas

Once llama-server is up, Atlas can use it directly:

```
atlas --backend=openai --api-base=http://127.0.0.1:8081 --model=qwen3-14b
```

`--model` is just a label for a single-model llama-server (it ignores the
name). If the server needs a key, set `ATLAS_API_KEY` (preferred, since a
`--api-key=` argument shows up in the process list). A real agent turn that
calls a tool is the other half of the decision: speed means nothing if the
model's tool calls break.

## Decision criteria (suggested)

These are starting points, not rules - adjust them once you see real numbers:

- **Roughly equal speed (within ~5%) and tool calling works the same:** stop at
  Phase 1. We still keep a useful extra backend (LM Studio, vLLM, any
  llama.cpp build), and Phase 2 isn't justified by speed.
- **Clearly faster decode (~15% or more) or much faster prefill on the long
  prompt:** Phase 2 is worth pursuing, since embedding gets the same engine
  without a separate server process.
- **Not faster, but the control matters** (e.g. reusing the prompt cache across
  the agent's iterations, KV-cache settings Ollama doesn't expose): note which
  control and why, and decide whether that alone justifies Phase 2.
- **Tool calling is worse or flaky on llama-server:** record which model and
  template; that is a Phase 2 cost, not just a footnote.

## Results

Measured 2026-10-10 on one machine, so treat small differences as noise.

| | |
|---|---|
| GPU / driver | AMD Radeon RX 9060 XT 16 GB (`gfx1200`), Windows driver 32.0.31041.1004 |
| Model | the GGUF Ollama ships for `qwen3:14b` (blob `sha256-a8cc1361f3145dc01f6d77c6c82c9116b9ffe3c97b34716fe20418455876c40e`) |
| Ollama | 0.35.1 |
| llama-server (ROCm) | lemonade-sdk `llama-b1342-windows-rocm-gfx120X-x64`; `llama-server --version`: 0.6.0-dev, build 11513, commit 71ad0590, Clang 24.0.0 |
| llama-server (Vulkan) | a prebuilt llama.cpp Vulkan zip (exact build not recorded) |
| llama-server flags | `-ngl 99 -c 8192 --flash-attn on --jinja --port 8081` |
| Script settings | 3 timed runs after a warm-up, median reported; temperature 0; 256-token cap; ctx 8192; a unique prompt per run so the prompt cache cannot help |

| prompt | backend | prompt tok | gen tok | TTFT (s) | prefill tok/s | decode tok/s (server) | decode tok/s (wall) |
|---|---|---:|---:|---:|---:|---:|---:|
| short | ollama (qwen3:14b) | 38 | 256 | 0.20 | 537.1 | 28.8 | 28.9 |
| short | llama-server ROCm | 37 | 256 | 0.19 | 570.9 | 28.7 | 29.0 |
| short | llama-server Vulkan | 35 | 256 | 0.28 | 188.8 | 30.5 | 30.8 |
| code-review | ollama (qwen3:14b) | 196 | 256 | 0.32 | 1034.8 | 30.5 | 30.7 |
| code-review | llama-server ROCm | 194 | 256 | 0.31 | 1045.3 | 29.9 | 30.1 |
| code-review | llama-server Vulkan | 194 | 256 | 0.92 | 249.0 | 30.0 | 30.3 |
| long-context | ollama (qwen3:14b) | 2657 | 256 | 2.78 | 1079.4 | 29.1 | 29.3 |
| long-context | llama-server ROCm | 2654 | 256 | 2.85 | 1041.3 | 25.7 | 25.9 |
| long-context | llama-server Vulkan | 2655 | 256 | 9.60 | 286.5 | 23.8 | 24.0 |

The ~30-token "short" prefill figures are overhead-dominated and not
comparable; judge prefill on the long-context rows.

### What the numbers say

- **ROCm llama-server vs. Ollama is a tie on short and medium prompts**
  (generation within 2%, first-token time within 0.01 s, prompt processing
  within 1%). This is expected: Ollama runs the same llama.cpp core on the same
  ROCm stack, so there is no hidden speed to unlock by running it ourselves.
- **Long prompt (2.6k tokens):** prompt processing is within 4% (1041 vs. 1079
  tok/s), but generation is about 12% slower on llama-server (25.7 vs. 29.1
  tok/s), and its three runs varied (27.8, 25.3, 25.7) while Ollama held steady.
  That looks like a setting rather than the engine; `--flash-attn on` is the
  first suspect and was not varied in this run.
- **Vulkan is much worse on this card** for prompt processing (about 4x slower
  than ROCm/Ollama) and long-context generation. It is not a useful option here.

### Decision (recommendation)

**Stop at Phase 1; do not pursue Phase 2 (#50) for speed.** By the criteria
above, speed is equal within a few percent, with one long-context generation gap
that is more likely a setting than a limitation. Embedding llama.cpp would give
the same performance as Ollama today in exchange for owning a GPU build matrix
(ROCm on Windows is the fragile part, as the setup notes above show).

Phase 2 would only be justified by control Ollama does not give us, not by
speed, for example a specific KV-cache or prompt-reuse behavior the agent loop
needs. If such a need appears, it should be measured against this baseline first.

### Not yet covered

- **Tool calling against a real llama-server - deliberately skipped.** Only
  speed was measured. The translation layer is covered by tests against a stub
  server, but no real model has called a tool through `--backend=openai`. It was
  judged not worth testing because the recommendation is not to build on this
  engine path; anyone adopting `llama-server` as their daily backend should try
  it first (needs `--jinja` and a tool-aware chat template).
- **`--flash-attn off`** (and other settings) for the long-context generation gap.
- Other models and quantizations; this is one model on one machine.

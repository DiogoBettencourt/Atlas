# Backend benchmark: Ollama vs. llama-server

Part of the own-inference-engine investigation (#47). This is the **decision
gate** between Phase 1 (an OpenAI-compatible backend, #49) and Phase 2 (an
embedded llama.cpp backend, #50): before building anything deeper, measure
whether running a ROCm-tuned llama.cpp directly is meaningfully better than
Ollama on the hardware we actually use.

"OpenAI-compatible" only names the request format (`/v1/chat/completions`).
No OpenAI service is involved - `llama-server` is llama.cpp's own HTTP server,
running on your machine.

**Status: not measured yet.** The code and this procedure exist; the numbers
have to come from the machine with the GPU (RX 9060 XT, 16 GB). Fill in the
results table at the bottom when you have them.

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
- **Nothing else using the GPU**, and a warmed-up run: the script does one
  untimed warm-up per backend and reports the median of the timed runs.

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

```
python scripts/bench_backends.py ^
    --ollama-model qwen3:14b ^
    --llama-url http://127.0.0.1:8081 ^
    --output bench-results.json
```

Needs Python 3.9+, standard library only. Useful options: `--runs` (default
3), `--max-tokens` (default 256), `--ctx` (default 8192), `--no-think` (asks
both servers to switch off reasoning output, best effort - reasoning models
otherwise spend part of the token budget thinking), and `--api-key` if the
server needs one.

It prints a markdown table (paste it below) and, with `--output`, a JSON file
with every individual run and the server versions.

**Reading the columns**

- *TTFT*: wall-clock seconds from sending the request to the first streamed
  token. Dominated by prompt processing for the long prompt.
- *prefill tok/s*: prompt-processing speed, as reported by the server.
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

Fill in after running on the RX 9060 XT.

| | |
|---|---|
| Date | |
| GPU / driver | |
| Model + quant (GGUF) | |
| Ollama version | |
| llama.cpp build (release tag or commit, HIP/Vulkan, `gfx` target) | |
| llama-server flags | |
| Context size / max tokens / runs | |

| prompt | backend | prompt tok | gen tok | TTFT (s) | prefill tok/s | decode tok/s (server) | decode tok/s (wall) |
|---|---|---:|---:|---:|---:|---:|---:|
| | | | | | | | |

**Decision:** _(stop at Phase 1 / proceed to Phase 2 / other - and why)_

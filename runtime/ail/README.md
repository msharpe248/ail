# AIL MLWSVM Runtime (`runtime/ail/`)

The AIL standard library and AI runtime are implemented directly in [**MLWSVM `0.30.0`**](https://github.com/msharpe248/mlws-vm) assembly (`.masm`) and automatically imported by compiler-generated programs.

## Module Architecture (Strictly Acyclic DAG)

MLWSVM requires an acyclic module import graph. The runtime is structured in four clean layers:

| Layer | Module | Responsibility |
|---|---|---|
| **0 (Leaf)** | [`config.masm`](config.masm) | Reads `.ail.env` / `.mlws.env` bindings (`Config.get`, `Config.has?`) and hot-reloads on `SIGHUP` |
| **0 (Leaf)** | [`sandbox.masm`](sandbox.masm) | Isolated `spawnm` + `cap.restrict` worker harness for sandboxed `tool` execution |
| **1 (Core)** | [`core.masm`](core.masm) | Single-`ref` tagged value model (`AilObj`, `AilSym`, `AilExc`, closures, lexical env maps), Ruby-like methods, `File`/`JSON`/`Env`/`Time`/`Math`/`Random`, `.inspect`, and `.debug_format` |
| **2 (Drivers)** | [`budget.masm`](budget.masm) | `within budget(...)` via MLWSVM `quota 6` inherited deadlines (`proc.deadline_ms`) + `retry_up_to` |
| **2 (Drivers)** | [`llm.masm`](llm.masm) | `openai` (Ollama/vLLM/`llama.cpp`), `anthropic`, and `mock` drivers + live SSE streaming via `std.http.open`/`read_chunk`/`close` |
| **2 (Drivers)** | [`mcp.masm`](mcp.masm) | Live `mcp_server` runtime (`--stdio` & Streamable HTTP `--http`) with `os.trap` graceful shutdown |
| **2 (Drivers)** | [`mcp_client.masm`](mcp_client.masm) | MCP client (`stdio` subprocess & Streamable HTTP) |
| **2 (Drivers)** | [`skill.masm`](skill.masm) | Inline `skill` and standard `SKILL.md` directory loader + progressive disclosure (`load_skill`) |
| **2 (Drivers)** | [`s1.masm`](s1.masm) | System 1 `decide` runtime (`jev`, `openai` fallback, `mock`) returning typed values with `.confidence` |
| **2 (Drivers)** | [`vector.masm`](vector.masm) | `Knowledge` flat `N×dim` `f32` vector store accelerated by `vec.normalize_into` and `vec.top_k` + metadata filters |
| **2.5** | [`context.masm`](context.masm) | Append-only multi-view `Context` (`:audit`, `drop`, `compact_before`, `limit_tokens`, `checkpoint`/`rewind`/`fork`, `memory`/`jsonl` `db`) |
| **3 (High-Level)** | [`agent.masm`](agent.masm) | Autonomous `agent` tool/MCP/skill/guard loop (`Agent.run` and live streaming `Agent.stream`) |
| **3 (High-Level)** | [`flow.masm`](flow.masm) | Stateful `flow` graph executor with conditional `when`/`unless` edges and runtime `max:` cycle enforcement |
| **3 (High-Level)** | [`a2a.masm`](a2a.masm) | Live A2A HTTP server (`/.well-known/agent.json`, `message/send`, `tasks/get`, `tasks/cancel`, `os.trap`) |
| **3 (High-Level)** | [`stubs.masm`](stubs.masm) | Inline chaining combinators (`-->` pipe, `|` fallback, `&` parallel MLWSVM processes) |

## Key MLWSVM Assembly Conventions

When modifying `.masm` files in this directory:
- **Per-bank parameter registers:** Parameters bind starting at index 0 per register bank (`(ref a, i64 b)` → `a=R0, b=I0`). Always allocate temporaries above parameter indices.
- **Typed `null` stores:** Never store literal `null` directly into `LOC[n]`, maps (`mset`), or lists (`lpush`) without a `ref` register (`mov.r LOC[n], null` or copy via an `R` register), otherwise MLWSVM tags it as `i64 0`.
- **JSON booleans:** Use `ncall "json.parse"` when serializing true JSON booleans (e.g. `{"stream":true}`) rather than storing raw integer `0`/`1`.

# AIL — AI Language

**AIL** is a general-purpose, Ruby-inspired programming language targeting [MLWSVM](../mlws-vm), built with first-class inference-side AI constructs: schemas, sandboxed tools, coded guardrails, scoped budgets, append-only multi-view contexts, System 1 decisions (Jev-style), agents, MCP servers & clients, Agent Skills, stateful flows (`-->`), and A2A agents.

## Status (`v0.4.0` — Targeting MLWSVM `v0.30.0`)

- **Full Language Frontend (C11, zero external deps):** Lexes, parses, and semantically verifies the entire AIL language (`use`, `schema`, `tool`, `guard`, `skill`, `decide`, `context`, `agent`, `flow`/`-->`, `within budget`, `retry_up_to`, `mcp_server`, `a2a_agent`, plus the Ruby-inspired general-purpose core).
- **Live Runtime on MLWSVM `v0.30.0` (`runtime/ail/*.masm`):**
  - Core Ruby-like language (primitives, exact decimals `0.25d`, durations `30.s`, symbols `:foo`, string interpolation, heredocs `<<~`, arrays, hashes, ranges, regex, classes, methods, closures/blocks, internal iterators `each`/`map`/`select`/`reject`/`reduce`/`flat_map`/`take`, `begin`/`rescue`/`ensure`/`raise`).
  - Unified config bindings via MLWSVM `.mlws.env` / `.ail.env` (`llm`, `s1`, `embedder`, `db`, `vector_db`, `mcp`, `skills`).
  - `schema` (validated records + compile-time JSON Schema generation), `tool` (VM-enforced `cap.restrict` sandboxing), coded `guard` hooks (`allow`/`reject`/`redact`/`escalate`), `within budget(...)` (enforced via MLWSVM **`quota 6`** inherited wall-clock deadlines + `proc.deadline_ms`), and `retry_up_to`.
  - Live `mcp_server` (`--stdio` & Streamable HTTP `--http`) + MCP clients (`stdio` & Streamable HTTP) + graceful **`os.trap`** (`SIGINT`/`SIGTERM`/`SIGHUP` `.ail.env` reload).
  - Agent Skills (`skill` inline declarations + standard `SKILL.md` directories with YAML frontmatter & progressive disclosure via `load_skill`).
  - Append-only multi-view `Context` (`:audit` vs composable `drop`/`compact_before`/`limit_tokens` views, `checkpoint`/`rewind`/`fork`, pluggable `memory`/`jsonl` `db` persistence).
  - Autonomous `agent` runtime with **live token streaming** (`Agent.stream { |tok| ... }` via MLWSVM **`std.http` `open`/`read_chunk`/`close`** + SSE framing), `openai`/Ollama/vLLM/`llama.cpp`, `anthropic`, and `mock` drivers.
  - System 1 `decide` (`jev`, `openai`/Ollama fallback, `mock` drivers) with typed values and calibrated `.confidence`.
  - Stateful `flow` graph runtime (`start`, `node`, inline `decide`, conditional `when`/`unless` edges, compile-time + runtime `max:` cycle bounds) and inline chaining operators (`-->`, `|`, `&`).
  - Pluggable RAG (`Knowledge` + flat `N×dim` `f32` `vector_db` accelerated by MLWSVM **`vec.top_k`** & **`vec.normalize_into`** batch natives + metadata pre-filtering).
  - Live `a2a_agent` HTTP server (`/.well-known/agent.json` Agent Card + JSON-RPC 2.0 `message/send`, `tasks/get`, `tasks/cancel`, plus `os.trap` graceful shutdown).

## Quick Start

```sh
make                         # builds ./ail (plain C11)
./ail version                # prints ail 0.4.0
./ail check examples/full_spec_check.ail
./ail run examples/hello.ail
./ail run examples/flow_rag_demo.ail --env examples/flow_rag.ail.env
./ail run examples/stream_ollama.ail --env examples/ollama_v03.ail.env
./ail run examples/agent_ollama.ail --env examples/ollama.ail.env
./ail run examples/mcp_repo_tools.ail -- --stdio
./ail run examples/a2a_support.ail --env examples/flow_rag.ail.env -- --http 127.0.0.1:9000
./ail bundle -o repo-tools examples/mcp_repo_tools.ail
make test                    # deterministic compiler, runtime, MCP, Agent, Flow, RAG, A2A & signal suite
make test-ollama             # live end-to-end agent, decide, and streaming tests against local Ollama
```

See [docs/SPEC.md](docs/SPEC.md) for the complete language specification and roadmap (`v0.1.0` → `v0.3.0`).

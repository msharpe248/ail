# AIL — AI Language

**AIL** is a general-purpose, Ruby-inspired programming language targeting [MLWSVM](../mlws-vm) (`0.30.0`), built with first-class inference-side AI constructs: schemas, sandboxed tools, coded guardrails, scoped budgets, append-only multi-view contexts, System 1 decisions (Jev-style), agents, MCP servers & clients, Agent Skills, stateful flows (`-->`), pluggable RAG, and A2A agents.

## Documentation

- **[Programming Guide](docs/GUIDE.md)** — Tutorial from first program to multi-file AI apps, streaming agents, flows, RAG, MCP, and A2A.
- **[Standard Library & Runtime Reference](docs/STDLIB.md)** — Complete method reference for primitives, collections, `File`/`JSON`/`Env`/`Time`/`Math`/`Random`, AI runtime types, exceptions, and `.ail.env` configuration.
- **[Worked Examples Index](docs/EXAMPLES.md)** — Catalog of runnable single-file and multi-file examples (mock + live Ollama).
- **[Language Specification](docs/SPEC.md)** — Formal grammar, semantics, and architecture notes.

## Status (`v0.5.0` — Targeting Released MLWSVM `0.30.0`)

- **Full Language Frontend (C11, zero external deps):** Lexes, parses, resolves multi-file `require`/`require_relative` (with diamond deduplication & circular-import detection), and statically checks the entire AIL language (`use`, `schema`, `tool`, `guard`, `skill`, `decide`, `context`, `agent`, `flow`/`-->`, `within budget`, `retry_up_to`, `mcp_server`, `a2a_agent`, plus the Ruby-inspired core).
- **Developer Tooling:**
  - `ail check` — Static type/schema/flow-cycle verification.
  - `ail run` — Compile and execute directly on MLWSVM `0.30.0` with signal forwarding (`SIGINT`/`SIGTERM`/`SIGHUP`).
  - `ail repl` — Interactive Read-Eval-Print Loop (`:help`, `:defs`, `:reset`, `:quit`) with `.inspect` formatting.
  - `ail debug` — Interactive MLWSVM debugger with `.loc` source mapping across `require`d files and `.debug_format core.debug_format` pretty-printing.
  - `ail bundle` — Ship any AIL program, MCP server, or A2A agent as a standalone executable binary.
- **Live Runtime on MLWSVM `0.30.0` (`runtime/ail/*.masm`):**
  - Core Ruby-like language & stdlib: primitives, exact decimals (`0.25d`), durations (`30.s`), symbols (`:foo`), string interpolation, heredocs (`<<~`), arrays (`compact`/`uniq`/`flatten`/`sort`/`group_by`/`each`/`map`/`select`/`reject`/`reduce`/`flat_map`/`take`), hashes (`fetch`/`delete`/`merge`), strings (`chomp`/`chars`/`slice`/`sub`/`gsub`), ranges, regex, classes, closures/blocks, `begin`/`rescue`/`ensure`/`raise`, plus `File`, `JSON`, `Env`, `Time`, `Math`, `Random`.
  - Unified config bindings via MLWSVM `.mlws.env` / `.ail.env` (`llm`, `s1`, `embedder`, `db`, `vector_db`, `mcp`, `skills`).
  - `schema`, VM-enforced `tool` sandboxing (`cap.restrict`), coded `guard`, `within budget(...)` (`quota 6` inherited wall-clock deadlines + `proc.deadline_ms`), and `retry_up_to`.
  - Live `mcp_server` (`--stdio` & Streamable HTTP `--http`) + MCP clients + graceful `os.trap` (`SIGINT`/`SIGTERM`/`SIGHUP`).
  - Agent Skills (`skill` inline + `SKILL.md` directories with progressive disclosure via `load_skill`).
  - Append-only multi-view `Context` (`:audit` vs composable views, `checkpoint`/`rewind`/`fork`, `memory`/`jsonl` `db` persistence).
  - Autonomous `agent` runtime with live token streaming (`Agent.stream { |tok| ... }` via MLWSVM `std.http` `open`/`read_chunk`/`close`), `openai`/Ollama/vLLM/`llama.cpp`, `anthropic`, and `mock` drivers.
  - System 1 `decide`, stateful `flow` graphs + inline chaining (`-->`, `|`, `&`), flat `f32` `Knowledge` RAG (`vec.top_k` & `vec.normalize_into`), and live `a2a_agent` HTTP servers.

## Quick Start

```sh
make                         # builds ./ail (plain C11)
./ail version                # prints ail 0.5.0
./ail repl                   # interactive AIL REPL
./ail check examples/full_spec_check.ail
./ail run examples/hello.ail
./ail run examples/support_desk/main.ail --env examples/support_desk/mock.ail.env
./ail run examples/flow_rag_demo.ail --env examples/flow_rag.ail.env
./ail run examples/stream_ollama.ail --env examples/ollama_v03.ail.env
./ail run examples/mcp_repo_tools.ail -- --stdio
./ail run examples/a2a_support.ail --env examples/flow_rag.ail.env -- --http 127.0.0.1:9000
./ail debug examples/hello.ail
./ail bundle examples/mcp_repo_tools.ail -o repo-tools
make test                    # deterministic compiler, runtime, multi-file, REPL, MCP, Agent, Flow, RAG & A2A suite
make test-ollama             # live end-to-end agent, decide, and streaming tests against local Ollama
```

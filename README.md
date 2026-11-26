# AIL — AI Language

**AIL** is a general-purpose, Ruby-inspired programming language targeting [MLWSVM](../mlws-vm), built with first-class inference-side AI constructs: schemas, sandboxed tools, coded guardrails, scoped budgets, append-only multi-view contexts, System 1 decisions (Jev-style), agents, MCP servers & clients, Agent Skills, stateful flows (`-->`), and A2A agents.

## Status (`v0.2.0`)

- **Full Language Frontend (C11, zero external deps):** Lexes, parses, and semantically verifies the entire AIL language (`use`, `schema`, `tool`, `guard`, `skill`, `decide`, `context`, `agent`, `flow`/`-->`, `within budget`, `retry_up_to`, `mcp_server`, `a2a_agent`, plus the Ruby-inspired general-purpose core).
- **Live Runtime on MLWSVM (`runtime/ail/*.masm`):**
  - Core Ruby-like language (primitives, exact decimals `0.25d`, durations `30.s`, symbols `:foo`, string interpolation, heredocs `<<~`, arrays, hashes, ranges, regex, classes, methods, closures/blocks, internal iterators `each`/`map`/`select`/`reject`/`reduce`/`flat_map`/`take`, `begin`/`rescue`/`ensure`/`raise`).
  - Unified config bindings via MLWSVM `.mlws.env` / `.ail.env` (`llm`, `s1`, `embedder`, `db`, `vector_db`, `mcp`, `skills`).
  - `schema` (validated records + compile-time JSON Schema generation).
  - `tool` (callable functions + VM-enforced `cap.restrict` sandboxing in isolated processes).
  - `guard` (coded `check_input`/`check_output`/`check_tool` guardrails with `allow`/`reject`/`redact`/`escalate`).
  - `within budget(...)` & `retry_up_to` (scoped limits, retries with backoff, isolated scope processes).
  - Live `mcp_server` over `--stdio` and Streamable HTTP (`--http host:port`), plus `MCP.connect` / agent `mcp` clients over `stdio` and Streamable HTTP.
  - Agent Skills (`skill` inline declarations + standard `SKILL.md` directories with YAML frontmatter & progressive disclosure via `load_skill`).
  - Append-only multi-view `Context` (`:audit` vs composable `drop`/`compact_before`/`limit_tokens` views, `checkpoint`/`rewind`/`fork`, and pluggable `memory`/`jsonl` `db` persistence).
  - Full `agent` tool-calling runtime with `openai` (OpenAI, Ollama, vLLM, `llama.cpp`), `anthropic`, and `mock` drivers, structured `returns` validation, and `monitor.event` telemetry.
  - Compile-time `flow` graph checker (reachability + bounded cycle enforcement) and `ail check` static verifier.

## Quick Start

```sh
make                         # builds ./ail (plain C11)
./ail version                # prints ail 0.2.0
./ail check examples/full_spec_check.ail
./ail run examples/hello.ail
./ail run examples/agent_ollama.ail --env examples/ollama.ail.env
./ail run examples/mcp_repo_tools.ail -- --stdio
./ail run examples/mcp_repo_tools.ail -- --http 127.0.0.1:8080
./ail bundle -o repo-tools examples/mcp_repo_tools.ail
make test                    # deterministic compiler, runtime, MCP, and agent suite
make test-ollama             # live end-to-end agent test against local Ollama
```

See [docs/SPEC.md](docs/SPEC.md) for the complete language specification and roadmap (`v0.1.0` → `v0.3.0`).

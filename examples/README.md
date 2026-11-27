# AIL Examples

This directory contains runnable examples covering every layer of AIL — from core Ruby-inspired syntax to multi-file agentic applications, MCP servers, A2A agents, and live local Ollama demos.

All examples are statically checked via `make check-examples`, and all deterministic examples are executed end-to-end by `make test`.

## Deterministic Offline Examples (No API Keys Required)

| Example | Config | What It Shows |
|---|---|---|
| [`hello.ail`](hello.ail) | *(none)* | Classes, blocks, iterators, exact decimals (`0.25d`), regex, `begin`/`rescue`/`ensure` |
| [`ai_features.ail`](ai_features.ail) | [`app.ail.env`](app.ail.env) | `.ail.env` bindings, `schema` validation, `cap.restrict` sandbox allow & deny, coded `guard`, multi-view `Context`, `within budget` (`quota 6`), `retry_up_to`, `-->`/`|`/`&` |
| [`mcp_repo_tools.ail`](mcp_repo_tools.ail) | *(none)* | Instant VM-sandboxed MCP server (`--stdio` & Streamable HTTP `--http`) with tools, resources, prompts, and `os.trap` shutdown |
| [`agent_mock_demo.ail`](agent_mock_demo.ail) | [`agent_mock.ail.env`](agent_mock.ail.env) | `agent` + live stdio MCP client + `SKILL.md` progressive disclosure ([`skills/`](skills/)) + inline `skill` + input/output guards + `Context` JSONL persistence + `Agent.stream` |
| [`flow_rag_demo.ail`](flow_rag_demo.ail) | [`flow_rag.ail.env`](flow_rag.ail.env) | System 1 `decide`, stateful `flow` with confidence escalation & bounded `max:` cycle, `Knowledge` flat `f32` RAG with `vec.top_k` & metadata filters |
| [`a2a_support.ail`](a2a_support.ail) | [`flow_rag.ail.env`](flow_rag.ail.env) | Live A2A HTTP server (`/.well-known/agent.json`, `message/send`, `tasks/get`, `os.trap` `SIGTERM`) backed by a System 1 `flow` |
| [`support_desk/`](support_desk/README.md) | [`support_desk/mock.ail.env`](support_desk/mock.ail.env) | **Worked multi-file application** using `require`, shared schemas, sandboxed tools, guards, RAG `Knowledge`, System 1 `decide`, stateful `flow`, and `agent` |
| [`chatbot/`](chatbot/README.md) | [`chatbot/mock.ail.env`](chatbot/mock.ail.env) | **Interactive streaming chatbot** loop with `gets`, `Context` windowing, `/history`, `/checkpoint`, `/rewind`, and PII redaction |
| [`coding_harness/`](coding_harness/README.md) | [`coding_harness/mock.ail.env`](coding_harness/mock.ail.env) | **Coding agent TUI harness** (Claude Code / Antigravity / Codex-style) with VM-sandboxed workspace tools, live tool-call UI hooks, and interactive `[y/N]` approval guard |
| [`full_spec_check.ail`](full_spec_check.ail) | *(none)* | Static verification (`./ail check examples/full_spec_check.ail`) exercising every language construct |

## Live Local Ollama Examples

These bind logical resource names to a local [Ollama](https://ollama.com) server (`http://127.0.0.1:11434/v1`, configured for `gemma4:31b-mlx` — edit the `.ail.env` files to use any model you have pulled):

| Example | Config | Run Command |
|---|---|---|
| [`agent_ollama.ail`](agent_ollama.ail) | [`ollama.ail.env`](ollama.ail.env) | `./ail run examples/agent_ollama.ail --env examples/ollama.ail.env` |
| [`flow_ollama.ail`](flow_ollama.ail) | [`ollama_v03.ail.env`](ollama_v03.ail.env) | `./ail run examples/flow_ollama.ail --env examples/ollama_v03.ail.env` |
| [`stream_ollama.ail`](stream_ollama.ail) | [`ollama_v03.ail.env`](ollama_v03.ail.env) | `./ail run examples/stream_ollama.ail --env examples/ollama_v03.ail.env` |
| [`support_desk/main.ail`](support_desk/main.ail) | [`support_desk/ollama.ail.env`](support_desk/ollama.ail.env) | `./ail run examples/support_desk/main.ail --env examples/support_desk/ollama.ail.env` |
| [`chatbot/main.ail`](chatbot/main.ail) | [`chatbot/ollama.ail.env`](chatbot/ollama.ail.env) | `./ail run examples/chatbot/main.ail --env examples/chatbot/ollama.ail.env` |
| [`coding_harness/main.ail`](coding_harness/main.ail) | [`coding_harness/ollama.ail.env`](coding_harness/ollama.ail.env) | `./ail run examples/coding_harness/main.ail --env examples/coding_harness/ollama.ail.env` |

Run the Ollama verification target with:
```sh
make test-ollama
```

# Worked Examples

Every example in `examples/` is verified by `make check-examples`, and the deterministic suite runs automatically in `make test`.

| Example | What It Demonstrates | Run Command |
|---|---|---|
| [hello.ail](../examples/hello.ail) | Ruby classes, closures, iterators, exact `.dec` math, regex, `begin`/`rescue`/`ensure` | `./ail run examples/hello.ail` |
| [ai_features.ail](../examples/ai_features.ail) | `.ail.env` config, `schema` validation, `cap.restrict` sandbox allow & deny, coded `guard`, multi-view `Context`, `within budget` (`quota 6`), `retry_up_to`, `-->`/`|`/`&` | `./ail run examples/ai_features.ail --env examples/app.ail.env` |
| [mcp_repo_tools.ail](../examples/mcp_repo_tools.ail) | Instant VM-sandboxed MCP server (`--stdio` & Streamable HTTP `--http`) with tools, resources, prompts, and `os.trap` graceful shutdown | `./ail run examples/mcp_repo_tools.ail -- --http 127.0.0.1:8080` |
| [agent_mock_demo.ail](../examples/agent_mock_demo.ail) | `agent` + live stdio MCP client + `SKILL.md` progressive disclosure + inline `skill` + input/output `guard`s + `Context` JSONL persistence + `Agent.stream` | `./ail run examples/agent_mock_demo.ail --env examples/agent_mock.ail.env` |
| [flow_rag_demo.ail](../examples/flow_rag_demo.ail) | System 1 `decide`, stateful `flow` with confidence escalation & bounded `max:` cycle, `Knowledge` flat `f32` RAG with `vec.top_k` & metadata filtering | `./ail run examples/flow_rag_demo.ail --env examples/flow_rag.ail.env` |
| [a2a_support.ail](../examples/a2a_support.ail) | Live A2A HTTP server (`/.well-known/agent.json`, `message/send`, `tasks/get`, `os.trap` `SIGTERM`) backed by a System 1 `flow` | `./ail run examples/a2a_support.ail --env examples/flow_rag.ail.env -- --http 127.0.0.1:9000` |
| [support_desk/main.ail](../examples/support_desk/main.ail) | **Worked multi-file application** using `require`, shared schemas, sandboxed tools, guards, RAG `Knowledge`, System 1 `decide`, stateful `flow`, and `agent` | `./ail run examples/support_desk/main.ail --env examples/support_desk/mock.ail.env` |
| [agent_ollama.ail](../examples/agent_ollama.ail) | Live local Ollama agent combining local tool calling, stdio MCP server, `SKILL.md`, `NoPII` guard, and multi-view `Context` | `./ail run examples/agent_ollama.ail --env examples/ollama.ail.env` |
| [flow_ollama.ail](../examples/flow_ollama.ail) | Live local Ollama System 1 `decide` classification | `./ail run examples/flow_ollama.ail --env examples/ollama_v03.ail.env` |
| [stream_ollama.ail](../examples/stream_ollama.ail) | Live local Ollama token-by-token streaming via MLWSVM 0.30.0 `std.http.open`/`read_chunk` | `./ail run examples/stream_ollama.ail --env examples/ollama_v03.ail.env` |
| [full_spec_check.ail](../examples/full_spec_check.ail) | Full-language static verification (`ail check`) covering every AIL construct | `./ail check examples/full_spec_check.ail` |

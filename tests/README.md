# AIL Test Suite (`tests/`)

## Running Tests

From the repository root:

```sh
# 31 deterministic end-to-end tests (no external APIs or network required)
make test

# Live end-to-end tests against a local Ollama server (http://127.0.0.1:11434)
make test-ollama
```

## What [`run_tests.sh`](run_tests.sh) Verifies

1. **Compiler & static checks (`ail check` / `ail build`):**
   - Checks all single-file and multi-file examples in `examples/`.
   - Rejects unbounded flow cycles at compile time ([`bad_unbounded_cycle.ail`](bad_unbounded_cycle.ail)).
   - Rejects circular `require` graphs at compile time ([`bad_cycle_a.ail`](bad_cycle_a.ail), [`bad_cycle_b.ail`](bad_cycle_b.ail)).
   - Verifies `ail version`, `.debug_format core.debug_format`, and cross-file `.loc` debug directives.
2. **Core Ruby-like runtime:** `examples/hello.ail`.
3. **AI constructs:** Config bindings, `schema`, `cap.restrict` sandbox allow & deny, `guard`, multi-view `Context`, `within budget` (`quota 6`), `retry_up_to`, `-->`/`|`/`&`.
4. **Live MCP Server:** Both `--stdio` and Streamable HTTP (`--http`) transports (`initialize`, `tools/list`, `tools/call`, `resources/list`, `resources/read`, `/health`).
5. **Agents, MCP Client, Skills & Context DB:** Multi-turn tool calling, live stdio MCP client, `SKILL.md` progressive disclosure, input/output guards, `Agent.stream`, and JSONL persistence.
6. **System 1, Stateful Flows & RAG:** `decide`, confidence escalation, bounded `max:` cycles, and flat `f32` `Knowledge` (`vec.top_k` + metadata filters).
7. **Live A2A Server & Signal Handling:** `/.well-known/agent.json`, `message/send`, `tasks/get`, and clean exit `0` on `SIGTERM` via `os.trap`.
8. **Standalone Bundle (`ail bundle`):** Builds and executes a self-contained binary.
9. **Multi-file App & Expanded Stdlib:** Full execution of [`examples/support_desk/main.ail`](../examples/support_desk/main.ail).
10. **Interactive REPL (`ail repl`):** Scripted session verifying expression evaluation, state persistence, and `.inspect` formatting.
11. **Interactive Chatbot ([`examples/chatbot/`](../examples/chatbot/README.md)):** Multi-turn streaming conversation, `:audit` history inspection, `/checkpoint`, and `/rewind`.
12. **Coding Agent TUI Harness ([`examples/coding_harness/`](../examples/coding_harness/README.md)):** Interactive `[y/N]` write-approval guard (both denial and approval), sandboxed `read_file`/`write_file`/`edit_file`, and live `on_tool_call`/`on_tool_result` hooks.

# Coding Agent TUI Harness (`examples/coding_harness/`)

A modular, terminal-based coding agent harness inspired by Claude Code, Antigravity TUI, and Codex CLI — written in pure multi-file AIL.

## Architecture

| File | Responsibility |
|---|---|
| [`ui.ail`](ui.ail) | ANSI terminal UI (colored banner, dividers, live tool-call cards, truncated previews, help table) |
| [`tools.ail`](tools.ail) | VM-sandboxed (`cap.restrict`) workspace tools: `list_files`, `read_file`, `search_files`, `write_file`, `edit_file`, `run_git_status` |
| [`guards.ail`](guards.ail) | `WorkspaceGuard` — blocks path traversal (`..` / leading `/`), prompts interactively (`[y/N]`) before file writes/edits, and redacts API keys/SSNs in output |
| [`main.ail`](main.ail) | Interactive TUI loop wiring `CodingAgent.stream` with live `on_tool_call` / `on_tool_result` hooks, multi-view `Context`, checkpoints, and slash commands |
| [`mock.ail.env`](mock.ail.env) | Deterministic offline scripted session (verified by `make test`) |
| [`ollama.ail.env`](ollama.ail.env) | Live local Ollama config (`gemma4:31b-mlx` with tool calling & streaming) |

## Running

From the repository root:

```sh
# Live interactive coding harness against local Ollama
./ail run examples/coding_harness/main.ail --env examples/coding_harness/ollama.ail.env

# Auto-approve file writes (optional)
AIL_AUTO_APPROVE=1 ./ail run examples/coding_harness/main.ail --env examples/coding_harness/ollama.ail.env

# Deterministic offline demo (approves write + edit via stdin)
printf "Update the demo file\ny\ny\n/quit\n" | \
  ./ail run examples/coding_harness/main.ail --env examples/coding_harness/mock.ail.env
```

## Slash Commands

| Command | Action |
|---|---|
| `/files [dir]` | List workspace files |
| `/tools` | List all registered VM-sandboxed tools |
| `/history` | Inspect the lossless `:audit` log |
| `/checkpoint` | Save a point-in-time checkpoint |
| `/rewind` | Rewind context to the saved checkpoint |
| `/approve` | Toggle `ask-on-write` vs `auto-approve` mode |
| `/clear` | Clear conversation context |
| `/quit` | Exit the harness |

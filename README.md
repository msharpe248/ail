# AIL — AI Language

**AIL** (*AI Language*) is a general-purpose, Ruby-inspired programming language targeting [**MLWSVM**](https://github.com/msharpe248/mlws-vm) (`0.30.0`), designed from the ground up with first-class inference-side AI primitives: typed `schema`s, VM-sandboxed `tool`s, programmable `guard`s, scoped `within budget(...)` deadlines, append-only multi-view `Context`s, System 1 `decide`, autonomous streaming `agent`s, instant `mcp_server`s & MCP clients, Agent Skills (`SKILL.md`), stateful `flow` graphs (`-->`), pluggable flat-`f32` RAG (`Knowledge`), and live `a2a_agent` servers.

AIL is written in **plain C11 with zero external compiler dependencies** and compiles directly to MLWSVM assembly (`.masm`) or standalone bundled binaries.

---

## Why AIL?

- **Code never hardcodes providers or databases:** Source files reference typed global resource registries (`$llm[:fast]`, `$s1[:router]`, `$vector_db[:kb]`, `$mcp[:repo_tools]`, `$db[:sessions]`) mapped 1-to-1 to `.ail.env` / `.mlws.env` — with `expect config do ... errors |errs| ... end` for startup contracts and graceful fallbacks. Swap between offline mocks, local Ollama/vLLM, and cloud APIs without editing a line of code.
- **VM-enforced safety, not prompt suggestions:** `tool` capabilities run inside MLWSVM `cap.restrict` sandboxes, `within budget(...)` enforces monotonic wall-clock deadlines via MLWSVM `quota 6`, and `flow` graphs reject unbounded cycles at compile time.
- **Ruby-like ergonomics:** Clean `def`/`class`/block/closure syntax, internal iterators, exact decimals (`0.25d`), durations (`30.s`), multi-file `require`, an interactive REPL (`ail repl`), and source-mapped debugging (`ail debug`).

---

## Documentation

| Document | Description |
|---|---|
| [Programming Guide](docs/GUIDE.md) | End-to-end tutorial from Hello World to multi-file agents, streaming, flows, RAG, MCP, and A2A |
| [Standard Library & Runtime Reference](docs/STDLIB.md) | Complete API reference for core types, stdlib modules, AI runtime objects, exceptions, and `.ail.env` |
| [Worked Examples Index](docs/EXAMPLES.md) | Catalog of runnable single-file, multi-file, mock, and live Ollama examples |
| [Language Specification](docs/SPEC.md) | Concise language overview, constructs, and version history (`v0.1.0`–`v0.5.0`) |

---

## Prerequisites & Build

AIL requires a C11 compiler (`cc`/`clang`/`gcc`) and [**MLWSVM `0.30.0+`**](https://github.com/msharpe248/mlws-vm) either in a sibling directory (`../mlws-vm/mlwsvm`), on your `PATH`, or pointed to via `MLWSVM=/path/to/mlwsvm`.

```sh
# 1. Build MLWSVM (if not already installed)
git clone https://github.com/msharpe248/mlws-vm.git
make -C mlws-vm

# 2. Build AIL
git clone https://github.com/msharpe248/ail.git
cd ail
make
./ail version          # ail 0.5.0
```

---

## Quick Start

```sh
# Run the core Ruby-like language demo
./ail run examples/hello.ail

# Start the interactive REPL
./ail repl

# Run the worked multi-file Support Desk app (require + RAG + System 1 + Flow + Agent + Guard)
./ail run examples/support_desk/main.ail --env examples/support_desk/mock.ail.env

# Run an instant VM-sandboxed MCP server (--stdio or Streamable HTTP)
./ail run examples/mcp_repo_tools.ail -- --stdio
./ail run examples/mcp_repo_tools.ail -- --http 127.0.0.1:8080

# Run a live A2A agent HTTP server (Agent Card + JSON-RPC 2.0 + graceful SIGTERM)
./ail run examples/a2a_support.ail --env examples/flow_rag.ail.env -- --http 127.0.0.1:9000

# Run live interactive chatbot, coding agent TUI harness & 3-agent A2A Poker against Ollama
./ail run examples/chatbot/main.ail --env examples/chatbot/ollama.ail.env
./ail run examples/coding_harness/main.ail --env examples/coding_harness/ollama.ail.env
./ail run examples/a2a_poker/table.ail --env examples/a2a_poker/ollama.ail.env

# Run live streaming & multi-file Support Desk against local Ollama
./ail run examples/stream_ollama.ail --env examples/ollama_v03.ail.env
./ail run examples/support_desk/main.ail --env examples/support_desk/ollama.ail.env

# Debug with .loc source mapping and pretty-printed AIL values
./ail debug examples/hello.ail

# Bundle any AIL program or MCP/A2A server into a standalone executable
./ail bundle examples/mcp_repo_tools.ail -o repo-tools
```

---

## CLI Commands

```text
ail check  <file.ail>                     Lex, parse, verify schemas & flow graphs
ail build  <file.ail> [-o out.masm]       Compile to MLWSVM assembly (.masm)
ail run    <file.ail> [--env FILE] [-- <args...>]
                                          Compile and execute on mlwsvm
ail debug  <file.ail> [--env FILE]        Launch mlwsvm debugger with .loc source info
ail repl   [--env FILE]                   Start interactive AIL REPL
ail bundle <file.ail> -o <bin>            Bundle into a standalone executable
ail version                               Print version
```

---

## Repository Layout

| Directory | Contents |
|---|---|
| [`src/`](src/README.md) | Zero-dependency C11 compiler (`lexer`, `parser`, `ast`, `require` loader, `sema`, `codegen`, CLI/REPL) |
| [`runtime/ail/`](runtime/ail/README.md) | Layered MLWSVM `0.30.0` runtime modules (`core`, `agent`, `llm`, `mcp`, `skill`, `context`, `budget`, `s1`, `flow`, `vector`, `a2a`) |
| [`examples/`](examples/README.md) | Runnable single-file & multi-file examples ([`support_desk/`](examples/support_desk/README.md)), plus mock and Ollama `.ail.env` configs |
| [`docs/`](docs/README.md) | Programming Guide, Standard Library Reference, Examples Index, and Language Spec |
| [`syntax/`](syntax/README.md) | Editor syntax highlighting for Vim/Neovim, VS Code/Cursor, Emacs, Nano, and `bat` |
| [`tests/`](tests/README.md) | End-to-end deterministic test suite (34 tests) and negative compile-time fixtures |

---

## Testing

```sh
make check-examples    # Static verification of every example
make test              # 26 deterministic compiler, runtime, multi-file, REPL, MCP, Agent, Flow, RAG, A2A & bundle tests
make test-ollama       # Live end-to-end Agent, System 1 decide, and SSE streaming tests against local Ollama
```

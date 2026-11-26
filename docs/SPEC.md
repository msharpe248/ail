# AIL — Language Specification (v0.1.0)

**AIL** (*AI Language*) is a general-purpose, Ruby-inspired programming language targeting **MLWSVM**, built from the ground up for AI agents, MCP servers & clients, A2A services, Agent Skills, tools, and multi-agent harnesses.

---

## 1. Unified Resource & Configuration Model

All external inference, storage, MCP, and skill resources are referenced by **kind + symbol** in source code and bound at runtime via MLWSVM `.mlws.env` / `.ail.env` files (`--env app.ail.env`):

| Kind | Role | Example Drivers |
|---|---|---|
| `llm` | Generative System 2 model | `openai` (also Ollama/vLLM/llama.cpp), `anthropic`, `mock` |
| `s1` | Discriminative System 1 model (Jev-style) | `jev`, `mock` |
| `embedder` | Text → embedding vector | `openai`, `mock` |
| `db` | Context / session / memory database | `memory`, `sqlite`, `postgres` |
| `vector_db` | Vector index + metric + filter | `memory` (flat `N×dim` `f32` matrix), `sqlite_vec`, `qdrant` |
| `mcp` | External MCP server connection | `stdio`, `http` (Streamable HTTP) |
| `skills` | Agent Skills catalog (`SKILL.md` folders) | `dir` |

Top-level preflight declarations:
```ruby
use llm: :reasoning, capabilities: [:tools, :structured]
use s1: :router
use db: :sessions
use vector_db: :kb
use mcp: :github
use skills: :workspace
```

---

## 2. Core Language

- **Primitives:** `nil`, `true`, `false`, `Int` (`42`, `1_000`), `Float` (`3.14`), `Decimal` (`0.25d`), `Duration` (`500.ms`, `30.s`, `2.m`), `String` (`"hi #{name}"`, `'raw'`, `<<~HEREDOC`), `Symbol` (`:ok`), `Regex` (`/\d+/i`).
- **Collections:** `Array` (`[1, 2]`), `Hash` (`{ a: 1, "b" => 2 }`), `Range` (`1..5`, `1...5`).
- **OOP & Closures:** `def`/`end`, `class Foo < Bar`, `module M` / `include M`, `@ivar`, blocks (`do |x| ... end`, `{ |x| ... }`), lambdas (`->(x) { ... }`), internal iterators (`each`, `map`, `flat_map`, `select`, `reject`, `reduce`, `any?`, `all?`, `find`, `take`, `drop`, `count`, `each_with_index`).
- **Control flow:** `if`/`elsif`/`else`/`end`, `unless`, `case`/`when`, `while`, `until`, `break`, `next`, `return`, statement modifiers (`x if cond`, `x unless cond`), `begin`/`rescue`/`ensure`/`raise`.

---

## 3. First-Class AI Constructs

- **`schema Name`** — typed record + compile-time JSON Schema + runtime validation.
- **`tool :name, "desc"`** — typed `param` / `returns`, optional `capabilities` (`cap.restrict` in isolated MLWSVM process) and `limits`.
- **`guard Name`** — programmable `check_input`, `check_output`, `check_tool` hooks (`allow`, `reject`, `redact`, `escalate`).
- **`skill :name, "desc"`** — inline skill declarations (`tools`, `capabilities`, `instructions`) interoperable with standard `SKILL.md` directories.
- **`decide using: :s1, on: expr`** — System 1 discriminative decision (`choice`, `score`, `prob`) returning typed values with `.confidence`.
- **`Context`** — append-only event log with non-destructive named `view` projections (including `:audit`), `checkpoint`/`fork`/`rewind`, and pluggable `db` / `vector_db` (`Knowledge`).
- **`within budget(...)` & `retry_up_to`** — lexically scoped budgets (compiled as an isolated `spawnm` scope process with MLWSVM `quota 6` wall-clock deadline + `spawnl` cancellation + per-process `@ail_ctx` propagation) and retry blocks with backoff.
- **`agent Name`** — autonomous tool/MCP/skill loop with `model`, `context_view`, `tools`, `mcp`, `skills`, `guards`, `capabilities`, `limits`, `returns`, `system`.
- **`flow Name` & `-->` / `&` / `|`** — composable pipelines and stateful graphs statically verified for node existence, reachability, and bounded cycles (`max:`).
- **`mcp_server "name", version: "..."`** — instant runnable MCP JSON-RPC 2.0 server (`--stdio` and Streamable HTTP `--http host:port`).
- **`a2a_agent "name", version: "..."`** — instant runnable A2A Agent Card + task HTTP server.

---

## 4. Roadmap

- **v0.1.0:** Full lexer/parser/AST + `ail check` verifier + core Ruby runtime + `schema` + sandboxed `tool` (`cap.restrict`) + coded `guard` + `within budget`/`retry_up_to` + live `mcp_server` (`--stdio` & Streamable HTTP).
- **v0.2.0:** `agent` runtime, `llm` drivers (`mock`, `openai`/local, `anthropic`), MCP client (`stdio` & Streamable HTTP), `SKILL.md` & inline `skill` runtime, multi-view `Context` + pluggable `db`.
- **v0.3.0:** `decide` (`jev` + `mock`), `flow` & `-->`/`&`/`|` parallel runtime, `Knowledge` + flat `f32` `vector_db` (`vec.*`), and `a2a_agent`.

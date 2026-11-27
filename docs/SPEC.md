# AIL — Language Specification (v0.5.0)

**AIL** (*AI Language*) is a general-purpose, Ruby-inspired programming language targeting [**MLWSVM**](https://github.com/msharpe248/mlws-vm) (`0.30.0`), built from the ground up for AI agents, MCP servers & clients, A2A services, Agent Skills, tools, and multi-agent harnesses.

For a hands-on tutorial and complete method reference, see the [Programming Guide](GUIDE.md) and [Standard Library Reference](STDLIB.md).

---

## 1. Unified Resource & Configuration Model

All external inference, storage, MCP, and skill resources are referenced by **kind + symbol** in source code and bound at runtime via MLWSVM `.mlws.env` / `.ail.env` files (`--env app.ail.env`):

| Kind | Role | Example Drivers |
|---|---|---|
| `llm` | Generative System 2 model | `openai` (also Ollama/vLLM/llama.cpp), `anthropic`, `mock` |
| `s1` | Discriminative System 1 model (Jev-style) | `jev`, `openai` (JSON-constrained fallback), `mock` |
| `embedder` | Text → embedding vector | `openai`, `mock` |
| `db` | Context / session / memory database | `memory`, `jsonl` |
| `vector_db` | Vector index + metric + filter | `memory` (flat `N×dim` `f32` matrix with `vec.top_k` / `vec.normalize_into`) |
| `mcp` | External MCP server connection | `stdio`, `http` (Streamable HTTP) |
| `skills` | Agent Skills catalog (`SKILL.md` folders) | `dir` |

Typed global resource registries (`$llm`, `$s1`, `$embedder`, `$db`, `$vector_db`, `$mcp`, `$skills`) map 1-to-1 to `.ail.env` keys (`$llm[:fast]` ↔ `llm.fast`). Use `expect config do ... errors |errs| ... end` for startup validation and graceful fallbacks:

```ruby
expect config do
  $llm[:reasoning], capabilities: [:tools, :structured]
  $s1[:router]
  $db[:sessions]
  $vector_db[:kb]
  $mcp[:github]
  $skills[:workspace]
errors |errs|
  errs.each do |e|
    if e.kind == :db
      $db[:sessions] = { driver: "memory" }   # runtime fallback via env.set
    else
      eputs "Config error for #{e.key}: #{e.reason}"
      OS.exit(1)
    end
  end
end
```

---

## 2. Core Language & Modularity

- **Primitives:** `nil`, `true`, `false`, `Int` (`42`, `1_000`), `Float` (`3.14`), `Decimal` (`0.25d`), `Duration` (`500.ms`, `30.s`, `2.m`), `String` (`"hi #{name}"`, `'raw'`, `<<~HEREDOC`), `Symbol` (`:ok`), `Regex` (`/\d+/i`).
- **Collections:** `Array` (`[1, 2]`), `Hash` (`{ a: 1, "b" => 2 }`), `Range` (`1..5`, `1...5`).
- **Multi-file modules:** `require "./path"` and `require_relative "./path"` with diamond deduplication, compile-time circular-import rejection, and per-file `.loc` debug info.
- **OOP & Closures:** `def`/`end`, `class Foo < Bar`, `module M` / `include M`, `@ivar`, blocks (`do |x| ... end`, `{ |x| ... }`), lambdas (`->(x) { ... }`), internal iterators (`each`, `map`, `flat_map`, `select`, `reject`, `reduce`, `any?`, `all?`, `find`, `take`, `drop`, `count`, `each_with_index`, `group_by`).
- **Control flow:** `if`/`elsif`/`else`/`end`, `unless`, `case`/`when`, `while`, `until`, `break`, `next`, `return`, statement modifiers (`x if cond`, `x unless cond`), `begin`/`rescue`/`ensure`/`raise`.
- **Standard library:** `File`, `JSON`, `Env`, `Time`, `Math`, `Random`, plus rich `String`, `Array`, and `Hash` methods and `.inspect` / `.debug_format`.

---

## 3. First-Class AI Constructs

- **`schema Name`** — typed record + compile-time JSON Schema + runtime validation.
- **`tool :name, "desc"`** — typed `param` / `returns`, optional `capabilities` (`cap.restrict` in isolated MLWSVM process) and `limits`.
- **`guard Name`** — programmable `check_input`, `check_output`, `check_tool` hooks (`allow`, `reject`, `redact`, `escalate`).
- **`skill :name, "desc"`** — inline skill declarations (`tools`, `capabilities`, `instructions`) interoperable with standard `SKILL.md` directories via progressive disclosure (`load_skill`).
- **`decide using: :s1, on: expr`** — System 1 discriminative decision (`choice`, `score`, `prob`) returning typed values with calibrated `.confidence`.
- **`Context`** — append-only event log with non-destructive named `view` projections (including `:audit`), `checkpoint`/`fork`/`rewind`, and pluggable `db` / `vector_db` (`Knowledge`).
- **`within budget(...)` & `retry_up_to`** — lexically scoped budgets (compiled as an isolated `spawnm` scope process with MLWSVM `quota 6` inherited wall-clock deadlines + `proc.deadline_ms` + `spawnl` cancellation) and retry blocks with backoff.
- **`agent Name`** — autonomous tool/MCP/skill loop with `model`, `context_view`, `tools`, `mcp`, `skills`, `guards`, `capabilities`, `limits`, `returns`, `system`, synchronous `.run`, and live SSE `.stream`.
- **`flow Name` & `-->` / `&` / `|`** — composable pipelines and stateful graphs statically verified for node existence, reachability, and bounded cycles (`max:`).
- **`mcp_server "name", version: "..."`** — instant runnable MCP JSON-RPC 2.0 server (`--stdio` and Streamable HTTP `--http host:port`) with `os.trap` graceful shutdown.
- **`a2a_agent "name", version: "..."`** — instant runnable A2A Agent Card (`/.well-known/agent.json`) + JSON-RPC 2.0 task HTTP server with `os.trap` graceful shutdown.

---

## 4. Release History

- **v0.1.0:** Full C11 lexer/parser/AST + `ail check` verifier + core Ruby runtime + `schema` + sandboxed `tool` (`cap.restrict`) + coded `guard` + `within budget`/`retry_up_to` + live `mcp_server` (`--stdio` & Streamable HTTP).
- **v0.2.0:** `agent` runtime, `llm` drivers (`mock`, `openai`/Ollama/vLLM/`llama.cpp`, `anthropic`), MCP client (`stdio` & Streamable HTTP), `SKILL.md` & inline `skill` runtime, multi-view `Context` + `jsonl` `db`.
- **v0.3.0:** System 1 `decide` (`jev`, `openai`, `mock`), stateful `flow` runtime + `-->`/`&`/`|`, flat `f32` `Knowledge` RAG, and live `a2a_agent` HTTP server.
- **v0.4.0:** MLWSVM `0.30.0` integration — live SSE streaming (`std.http.open`/`read_chunk`/`close`), inherited `quota 6` wall-clock deadlines (`proc.deadline_ms`), batch vector ops (`vec.top_k`, `vec.normalize_into`), and `os.trap` signal handling (`SIGINT`/`SIGTERM`/`SIGHUP`).
- **v0.5.0:** Multi-file `require`/`require_relative` with diamond deduplication & cycle detection, interactive `ail repl`, `ail debug` + `.debug_format`, expanded stdlib (`Env`, `Time`, `Math`, `Random`, collection/string methods), Programming Guide, Stdlib Reference, and worked multi-file `support_desk` application.

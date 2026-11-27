# The AIL Programming Guide

**AIL** (*AI Language*) is a general-purpose, Ruby-inspired programming language compiled to **MLWSVM 0.30.0** bytecode/assembly. It treats inference-side AI building blocks—schemas, sandboxed tools, coded guardrails, scoped budgets, append-only multi-view contexts, System 1 decisions, agents, MCP servers & clients, Agent Skills, stateful flows, RAG vector knowledge, and A2A services—as first-class language constructs.

This guide walks through AIL from your first script to multi-file agent harnesses and standalone bundled executables.

---

## Table of Contents

1. [Getting Started & CLI](#1-getting-started--cli)
2. [Core Language (Ruby-Inspired General Purpose)](#2-core-language-ruby-inspired-general-purpose)
3. [Unified Configuration Bindings (`.ail.env`)](#3-unified-configuration-bindings-ailenv)
4. [Schemas & Validation (`schema`)](#4-schemas--validation-schema)
5. [Tools & VM-Enforced Sandboxing (`tool`, `capabilities`)](#5-tools--vm-enforced-sandboxing-tool-capabilities)
6. [Coded Guardrails (`guard`)](#6-coded-guardrails-guard)
7. [Append-Only Multi-View `Context` & Memory DBs](#7-append-only-multi-view-context--memory-dbs)
8. [Scoped Budgets & Retries (`within`, `retry_up_to`)](#8-scoped-budgets--retries-within-retry_up_to)
9. [Agents, Tool Calling & Live Token Streaming (`agent`)](#9-agents-tool-calling--live-token-streaming-agent)
10. [MCP Servers & MCP Clients (`mcp_server`, `MCP.connect`)](#10-mcp-servers--mcp-clients-mcp_server-mcpconnect)
11. [Agent Skills (`skill` & `SKILL.md` Progressive Disclosure)](#11-agent-skills-skill--skillmd-progressive-disclosure)
12. [System 1 Decisions (`decide`)](#12-system-1-decisions-decide)
13. [Stateful Graphs (`flow`) & Inline Chaining (`-->`, `|`, `&`)](#13-stateful-graphs-flow--inline-chaining----)
14. [Pluggable RAG & Vector Search (`Knowledge`)](#14-pluggable-rag--vector-search-knowledge)
15. [A2A Agent Servers (`a2a_agent`)](#15-a2a-agent-servers-a2a_agent)
16. [Multi-File Projects (`require`), REPL, Debugging & Bundling](#16-multi-file-projects-require-repl-debugging--bundling)

---

## 1. Getting Started & CLI

Build the C11 compiler (zero external dependencies) and verify MLWSVM 0.30.0 is available:

```sh
make
./ail version          # ail 0.5.0
```

The `ail` CLI provides seven subcommands:

| Command | Purpose |
|---|---|
| `ail check <file.ail>` | Lex, parse, generate JSON Schemas, verify `flow` graphs & `require`s without running |
| `ail run <file.ail> [--env FILE] [-- <args>]` | Compile to `.masm` and execute on `mlwsvm` |
| `ail build <file.ail> [-o out.masm]` | Emit readable MLWSVM assembly (`.masm`) with `.loc` source mappings |
| `ail debug <file.ail> [--env FILE]` | Launch `mlwsvm debug` with source lines and `.debug_format` value formatting |
| `ail repl [--env FILE]` | Start an interactive AIL REPL |
| `ail bundle <file.ail> -o <binary>` | Package the VM, runtime, and your program into a single standalone binary |
| `ail version` | Print compiler version |

---

## 2. Core Language (Ruby-Inspired General Purpose)

AIL adopts Ruby's clean, expression-oriented syntax and object model:

```ruby
class Greeter
  def initialize(prefix)
    @prefix = prefix
  end

  def greet(name)
    "#{@prefix}, #{name}!"
  end
end

g = Greeter.new("Hello")
puts g.greet("AIL")
```

### Values & Literals
* **Numbers:** `42`, `1_000_000`, `0xFF`, `3.14`, and **exact decimals** `0.25d` (`0.10d + 0.20d == 0.30d`—ideal for cost budgets without floating-point drift).
* **Durations:** `500.ms`, `30.s`, `2.m`.
* **Strings:** `"interpolated #{x}"`, `'literal'`, and indented heredocs (`<<~PROMPT ... PROMPT`).
* **Symbols:** `:billing`, `:tech`, `:prompt_injection?` (backed by MLWSVM `text.intern` for O(1) identity comparison).
* **Regex:** `/\b\d{3}-\d{2}-\d{4}\b/i` (linear-time Pike VM; immune to catastrophic backtracking).
* **Collections:** Arrays `[1, 2, 3]`, insertion-ordered Hashes `{ host: "localhost", port: 8080 }`, Ranges `1..5` (inclusive) and `1...5` (exclusive).

### Blocks, Closures & Internal Iterators
Blocks (`do |x| ... end` or `{ |x| ... }`) capture and can mutate enclosing local variables just like Ruby:

```ruby
nums = [1, 2, 3, 4, 5, 6]
evens = nums.select { |n| n.even? }.map { |n| n * 2 }

total = 0
evens.each do |x|
  total += x
end
puts total   # => 24
```

### Control Flow & Exceptions
`if`/`elsif`/`else`/`end`, `unless`, `case`/`when`, `while`/`until`, trailing modifiers (`return x if done?`), and structured exceptions:

```ruby
res = begin
  raise RuntimeError, "transient"
rescue RuntimeError => e
  "recovered: #{e.message}"
ensure
  # always runs
end
```

---

## 3. Typed Resource Registries (`$llm`, `$db`, ...) & `expect config`

AIL code **never** hardcodes whether an LLM is local (Ollama, vLLM, `llama.cpp`) or remote (OpenAI, Anthropic), nor whether a database is in-memory, JSONL, or external. Instead, AIL provides seven built-in **typed resource registries** that map 1-to-1 to keys in your runtime `.ail.env` file:

| AIL Resource Reference | `.ail.env` Key | Role |
|---|---|---|
| `$llm[:fast]` | `llm.fast` | Generative System 2 model |
| `$s1[:router]` | `s1.router` | Discriminative System 1 classifier |
| `$embedder[:docs]` | `embedder.docs` | Text → embedding vector |
| `$db[:sessions]` | `db.sessions` | Context / session persistence DB |
| `$vector_db[:kb]` | `vector_db.kb` | Vector index (`Knowledge` RAG) |
| `$mcp[:repo_tools]` | `mcp.repo_tools` | External MCP server |
| `$skills[:local]` | `skills.local` | Agent Skills (`SKILL.md`) catalog |

```properties
# Local Ollama — source code doesn't know it's local
llm.fast = {"driver": "openai", "url": "http://127.0.0.1:11434/v1", "model": "gemma4:31b-mlx"}

# Remote API with ${VAR} substitution & secret redaction in the VM monitor
secret llm.reasoning = {"driver": "anthropic", "model": "claude-sonnet-4-20250514", "api_key": "${ANTHROPIC_API_KEY}"}

# System 1 classifier (Jev, OpenAI/Ollama fallback, or mock)
s1.router = {"driver": "jev", "url": "https://api.typesafe.ai/v1", "api_key": "${JEV_API_KEY}"}

# Embeddings, Memory DB, Vector DB, MCP servers, and Skills catalogs
embedder.docs  = {"driver": "openai", "url": "http://127.0.0.1:11434/v1", "model": "nomic-embed-text"}
db.sessions    = {"driver": "jsonl", "path": "./data/sessions.jsonl"}
vector_db.kb   = {"driver": "memory", "metric": "cosine", "embedder": "docs"}
mcp.repo_tools = {"transport": "stdio", "cmd": ["./ail", "run", "examples/mcp_repo_tools.ail", "--", "--stdio"]}
skills.local   = {"driver": "dir", "paths": ["./examples/skills"]}
```

### Validating Config & Graceful Fallbacks (`expect config`)
Use `expect config do ... end` at startup to verify required bindings (and optional `capabilities:` or `driver:` constraints) all at once. If you include an `errors |errs|` block, you can inspect each failure (`e.kind`, `e.name`, `e.key`, `e.reason`) and either register a runtime default directly into the VM environment (`$db[:sessions] = { ... }`) or exit cleanly:

```ruby
expect config do
  $llm[:fast], capabilities: [:tools, :structured]
  $s1[:router]
  $db[:sessions]
errors |errs|
  errs.each do |e|
    if e.kind == :db
      $db[:sessions] = { driver: "memory" }   # graceful in-memory fallback
    else
      eputs "Config error for #{e.key}: #{e.reason}"
      OS.exit(1)
    end
  end
end

# Inspect configured fields directly:
puts $llm[:fast]["model"]
puts $db[:sessions].exists?
```

Without an `errors` clause, `expect config` raises `ConfigError` listing every missing or incompatible binding.

Run with `--env`:
```sh
./ail run app.ail --env app.ail.env
```

---

## 4. Schemas & Validation (`schema`)

While general AIL code is dynamically typed, protocol and AI boundaries use `schema` to generate compile-time JSON Schemas and validate data at runtime:

```ruby
schema Ticket
  field :id,       Int,    "Unique ticket ID"
  field :title,    String, "Short summary"
  field :priority, Int = 3
  field :queue,    Enum[:billing, :tech, :fraud]
end

t = Ticket.new(id: 42, title: "Double charge", queue: :billing)
puts t.priority          # => 3
puts Ticket.json_schema  # => {"type":"object","properties":{...},"required":["id","title","queue"]}
```

Missing required fields raise `SchemaError`. Supported boundary types: `String`, `Int`, `Float`, `Decimal`, `Bool`, `Any`, `Array[T]`, `Hash[K, V]`, `Optional[T]` (or `T?`), `Enum[:a, :b]`, and other `schema` names.

---

## 5. Tools & VM-Enforced Sandboxing (`tool`, `capabilities`)

A `tool` compiles its `param` list into a JSON Schema automatically and can be called like a function, attached to an `agent`, or exposed by an `mcp_server`:

```ruby
tool :read_local, "Read a file inside the current workspace" do
  param :path, String, "Relative file path"
  returns String

  # Enforced at the MLWSVM boundary via cap.restrict in an isolated child process!
  capabilities fs: read_only("./"), net: :none, run: :none

  File.read(path)
end
```

When `capabilities` is declared, invoking the tool spawns a lightweight (~4 KB) isolated MLWSVM process, applies `cap.restrict`, runs the body, and returns the result. Attempting to read `/etc/hosts` or open a socket immediately raises `PermissionError` (`E_PERMISSION`)—the host process's own privileges remain untouched.

---

## 6. Coded Guardrails (`guard`)

Guardrails are programmable AIL objects with `check_input`, `check_output`, and `check_tool` hooks. Inside any hook, return `allow`, `reject("reason")`, `redact(new_value)`, or `escalate("reason")`:

```ruby
guard NoPII do
  check_input do |msg, ctx|
    return reject("Prompt injection") if msg.include?("IGNORE PREVIOUS")
    allow
  end

  check_output do |msg, ctx|
    if msg.match?(/\b\d{3}-\d{2}-\d{4}\b/)
      return redact(msg.gsub(/\b\d{3}-\d{2}-\d{4}\b/, "[REDACTED]"))
    end
    allow
  end
end
```

Guard hooks can run arbitrary Ruby logic, linear-time regexes, or even a fast System 1 `decide` classifier.

---

## 7. Append-Only Multi-View `Context` & Memory DBs

`Context` is an **append-only event log**—events are never destructively stripped. Instead, you project named **views** over the underlying log while `:audit` always preserves the full lossless history:

```ruby
ctx = Context.new(db: $db[:sessions])
ctx.append(:user, "Investigate the crash")
snap = ctx.checkpoint
ctx.append(:tool_intermediate, "noisy grep output...")
ctx.append(:assistant, "Found root cause in parser.c")

ctx.define_view :llm_compact do |log|
  log
    .drop(:tool_intermediate)
    .compact_before(-6, using: $llm[:fast])
    .limit_tokens(8000)
end

puts ctx.view(:audit).size        # lossless full history
puts ctx.view(:llm_compact).size  # compacted view for the LLM

branch = ctx.fork                 # independent in-memory branch
ctx.rewind(snap)                  # rewind to earlier checkpoint
```

---

## 8. Scoped Budgets & Retries (`within`, `retry_up_to`)

`within budget(...)` executes its body in an isolated MLWSVM scope process with an inherited monotonic wall-clock deadline (`quota 6`). Every nested call, child process, and HTTP timeout automatically respects the same remaining deadline (`proc.deadline_ms()`):

```ruby
answer = within budget(timeout: 5.s, cost: 0.10d) do
  retry_up_to 3, backoff: :exponential, on: [RuntimeError, GuardViolation] do |attempt|
    Researcher.run(query, context: ctx)
  end
rescue BudgetExceeded => e
  "Timed out: #{e.message}"
end
```

---

## 9. Agents, Tool Calling & Live Token Streaming (`agent`)

An `agent` wires together a logical `model`, `system` prompt, local `tools`, external `mcp` servers, `skills`, `guards`, `context_view`, `capabilities`, `limits`, and optional structured `returns Schema`:

```ruby
agent LocalAssistant do
  model        $llm[:local]
  context_view :llm_compact
  tools        [multiply]
  mcp          [{ $mcp[:repo_tools] => { only: [:word_count] } }]
  skills       [$skills[:local]]
  guards       [NoPII]
  limits       max_turns: 6, tokens: 20_000, retries: 1

  system <<~PROMPT
    You are a concise engineering assistant. Use tools whenever exact answers are needed.
  PROMPT
end

# Standard synchronous run
reply = LocalAssistant.run("Compute 37 * 43", context: ctx)

# Live token-by-token streaming (MLWSVM v0.30.0 std.http.open / read_chunk)
full = LocalAssistant.stream("Summarize AIL in one sentence.") do |tok|
  print tok
end
```

---

## 10. MCP Servers & MCP Clients (`mcp_server`, `MCP.connect`)

### Building an Instant MCP Server
Declaring `mcp_server` makes the file immediately runnable over `--stdio` or Streamable HTTP (`--http host:port`), with auto-generated JSON Schemas, VM capability sandboxing, and graceful `SIGINT`/`SIGTERM`/`SIGHUP` handling via `os.trap`:

```ruby
mcp_server "repo-tools", version: "0.1.0" do
  capabilities fs: read_only("./"), net: :none, run: :none
  expose search_code

  tool :word_count, "Count words in text" do
    param :text, String, "Input text"
    returns Int
    text.strip.split.size
  end

  resource "repo://readme", mime: "text/markdown" do
    File.read("README.md")
  end
end
```

Run it directly or ship it as a single binary:
```sh
./ail run examples/mcp_repo_tools.ail -- --stdio
./ail run examples/mcp_repo_tools.ail -- --http 127.0.0.1:8080
./ail bundle examples/mcp_repo_tools.ail -o repo-tools
```

### Connecting to MCP Servers
Agents attach external MCP servers via `$mcp[:name]` (`mcp [$mcp[:repo_tools]]`, or filtered `[{ $mcp[:repo_tools] => { only: [:word_count] } }]`). You can also invoke an MCP server directly from code:

```ruby
cl = MCP.connect($mcp[:repo_tools])
puts cl.call_tool(:word_count, { text: "hello from ail" })
puts cl.read_resource("repo://readme")
cl.close
```

---

## 11. Agent Skills (`skill` & `SKILL.md` Progressive Disclosure)

Agents natively support both standard `SKILL.md` directories (YAML frontmatter `name` + `description` + markdown body) and inline AIL `skill` blocks:

```ruby
skill :style_guide, "Keep replies crisp and structured" do
  tools [check_format]
  instructions <<~MD
    Keep all responses under 3 sentences and cite exact numbers.
  MD
end
```

At startup, only each skill's lightweight `name` and `description` are added to the agent's catalog prompt. When a task matches, the agent calls the built-in `load_skill(name)` tool, which loads the full instructions on demand and tags the event as `:skill_body` in `Context`.

---

## 12. System 1 Decisions (`decide`)

`decide` provides fast, schema-constrained discriminative inference (Jev-style `choice`, `score`, `prob`) returning typed values with calibrated `.confidence`:

```ruby
d = decide using: $s1[:router], on: ticket_text do
  choice :queue, [:billing, :tech, :fraud]
  score  :urgency, 1..5
  prob   :duplicate?
end

if d.queue == :billing && d.queue.confidence >= 0.85
  puts "Fast-path billing (urgency=#{d.urgency})"
else
  puts "Low confidence (#{d.queue.confidence}), escalating to System 2"
end
```

---

## 13. Stateful Graphs (`flow`) & Inline Chaining (`-->`, `|`, `&`)

### Inline Composable Operators
* `a --> b`: sequential pipeline
* `a | b`: fallback (run `b` if `a` raises)
* `a & b`: parallel fan-out across isolated MLWSVM processes returning `[res_a, res_b]`

### Stateful `flow` Graphs
`flow` declarations are **statically verified** by `ail check`: unreachable nodes trigger warnings, and **any cycle without a `max: N` bound is a compile-time error**:

```ruby
flow SupportFlow
  start :intake
  node :intake --> :triage

  decide :triage, using: $s1[:router] do
    choice :queue, [:billing, :tech, :fraud]
    score  :urgency, 1..5
  end

  edge :triage   --> :billing,     when: ->(d) { d.queue == :billing && d.queue.confidence >= 0.85 }
  edge :triage   --> :tech,        when: ->(d) { d.queue == :tech    && d.queue.confidence >= 0.85 }
  edge :triage   --> :deep_review
  edge :billing  --> :draft
  edge :tech     --> :draft
  edge :deep_review --> :draft
  edge :draft    --> :critique
  edge :critique --> :draft, unless: ->(s) { s["approved"] }, max: 2
  edge :critique --> :done
end
```

---

## 14. Pluggable RAG & Vector Search (`Knowledge`)

`Knowledge` binds to a named `vector_db` and `embedder`, storing vectors in a single flat `N × dim` `f32` matrix accelerated by MLWSVM 0.30.0's `vec.normalize_into` and `vec.top_k` batch natives:

```ruby
kb = Knowledge.new(vector_db: $vector_db[:kb])
kb.ingest("doc1", "MLWSVM register machine written in C11", { lang: "c" })
kb.ingest("doc2", "AIL Ruby syntax agents and MCP servers", { lang: "ail" })

hits = kb.search("register virtual machine", top_k: 3, filter: { lang: "c" })
ctx.attach(:knowledge, hits)
```

---

## 15. A2A Agent Servers (`a2a_agent`)

Wrap any `Agent`, `Flow`, `Tool`, or lambda as a live Agent-to-Agent (A2A) HTTP server:

```ruby
a2a_agent "support-router", version: "0.5.0" do
  description "Triages support tickets via System 1 and bounded flows"
  handler SupportFlow
end
```

Running `./ail run a2a_support.ail --env flow_rag.ail.env -- --http 127.0.0.1:9000` automatically serves:
* `GET /.well-known/agent.json` (auto-generated A2A Agent Card)
* `GET /health`
* `POST /` (JSON-RPC 2.0: `message/send`, `tasks/send`, `tasks/get`, `tasks/cancel`)
* Graceful `SIGINT`/`SIGTERM` shutdown and `SIGHUP` `.ail.env` reload.

---

## 16. Multi-File Projects (`require`), REPL, Debugging & Bundling

### Splitting Code Across Files
Use `require` (or `require_relative`) with paths relative to the current file (`.ail` extension is optional). Diamond imports are deduplicated and circular requires are caught at compile time:

```ruby
require "./schemas"
require "./guards"
require "./tools"
```

### Interactive REPL
```sh
./ail repl --env examples/app.ail.env
ail> x = 0.10d + 0.20d
=> ok
ail> x
=> 0.30d
ail> [1, 2, 3].map { |n| n * 10 }
=> [10, 20, 30]
```

### Debugging & Standalone Binaries
* **Source-level debugging:** `./ail debug my_app.ail --env app.ail.env` opens `mlwsvm debug` with `.loc` lines pointing directly to your `.ail` files (including required files) and `.debug_format core.debug_format` formatting AIL values cleanly.
* **Single-file shipping:** `./ail bundle my_app.ail -o my_app` produces a self-contained native executable with `--opt 2` optimization enabled.

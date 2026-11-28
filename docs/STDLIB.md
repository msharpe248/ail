# AIL Standard Library & Runtime Reference (`v0.5.0`)

Complete reference for built-in types, methods, standard library modules, AI runtime objects, exceptions, and `.ail.env` configuration schemas.

---

## 1. Universal Methods (All Values)

| Method | Returns | Description |
|---|---|---|
| `.to_s` | `String` | Human-readable string representation |
| `.inspect` | `String` | Ruby-style developer representation (`"hi"`, `:sym`, `nil`, `0.25d`, `[1, 2]`) |
| `.nil?` | `Bool` | `true` only for `nil` |

---

## 2. Core Types

### `Int` & `Float`
| Method | Returns | Description |
|---|---|---|
| `.ms`, `.s`, `.m` | `Duration` | Construct duration in milliseconds, seconds (`* 1000`), or minutes (`* 60000`) |
| `.times { \|i\| ... }` | `Int` | Yield `0`, `1`, ..., `n - 1` |
| `.abs` | `Int` | Absolute value |
| `.even?`, `.odd?` | `Bool` | Parity predicates |
| `.to_i`, `.to_f` | `Int` / `Float` | Numeric conversions |

### `Decimal` (`0.25d`)
Exact arbitrary-precision base-10 decimal backed by MLWSVM `.dec`.
* Operators: `+`, `-`, `*`, `/`, unary `-`, `==`, `!=`, `<`, `<=`, `>`, `>=` (mixes freely with `Int`).
* Methods: `.round(places = 0)` (half-even rounding via `dec.round`), `.to_f`, `.to_s`, `.inspect`.

### `Duration` (`30.s`, `500.ms`)
* `.ms` → `Int` (duration in milliseconds).

### `String`
| Method | Returns | Description |
|---|---|---|
| `.length`, `.size` | `Int` | Unicode character count (`text.length`) |
| `.empty?` | `Bool` | `true` if byte length is 0 |
| `.upcase`, `.downcase` | `String` | Full Unicode case mapping |
| `.strip`, `.trim` | `String` | Remove leading & trailing Unicode whitespace |
| `.chomp` | `String` | Remove a single trailing `\n` or `\r\n` |
| `.split(sep = "")` | `Array[String]` | Split on separator (empty string splits on whitespace runs) |
| `.lines` | `Array[String]` | Split into lines |
| `.chars` | `Array[String]` | Split into Unicode grapheme clusters |
| `.slice(start, len)` | `String` | Substring slice |
| `.include?(sub)` | `Bool` | Substring containment |
| `.start_with?(prefix)`, `.end_with?(suffix)` | `Bool` | Prefix / suffix check |
| `.match?(regex_or_str)` | `Bool` | Linear-time regex test (also `=~` and `!~`) |
| `.gsub(pat, repl)` / `.replace(pat, repl)` | `String` | Replace all matches (string or regex) |
| `.sub(pat, repl)` | `String` | Replace first match (string or regex) |
| `.to_i`, `.to_f` | `Int` / `Float` | Parse numeric string |

### `Array`
| Method | Returns | Description |
|---|---|---|
| `[idx]`, `[idx] = val` | `Any` | Index access (supports negative indices) |
| `.length`, `.size`, `.count` | `Int` | Element count |
| `.empty?` | `Bool` | `true` if empty |
| `.first`, `.last` | `Any` | First / last element, or `nil` |
| `.push(v)`, `.pop` | `Array` / `Any` | Append / remove last element |
| `.join(sep = "")` | `String` | Join stringified elements |
| `.reverse` | `Array` | New reversed array |
| `.include?(v)` | `Bool` | Value equality membership check |
| `.take(n)`, `.drop(n)` | `Array` | First `n` / remaining elements |
| `.compact` | `Array` | New array with `nil` elements removed |
| `.uniq` | `Array` | New array with duplicates removed (order preserved) |
| `.flatten` | `Array` | One-level flattened array |
| `.sort` | `Array` | New stably sorted array |
| `.each { \|x\| ... }` | `Array` | Iterate elements |
| `.each_with_index { \|x, i\| ... }` | `Array` | Iterate elements with 0-based index |
| `.map { \|x\| ... }` | `Array` | Transform elements |
| `.flat_map { \|x\| ... }` | `Array` | Map and flatten one level |
| `.select { \|x\| ... }` / `.filter` | `Array` | Keep elements where block is truthy |
| `.reject { \|x\| ... }` | `Array` | Drop elements where block is truthy |
| `.reduce(init?) { \|acc, x\| ... }` / `.inject` | `Any` | Left fold |
| `.any? { \|x\| ... }`, `.all? { \|x\| ... }` | `Bool` | Short-circuiting existential / universal check |
| `.find { \|x\| ... }` | `Any` | First matching element or `nil` |
| `.group_by { \|x\| ... }` | `Hash` | Group elements by stringified key returned from block |

### `Hash`
| Method | Returns | Description |
|---|---|---|
| `[key]`, `[key] = val`, `.key` | `Any` | Key lookup / assignment (dot access falls back to string key) |
| `.length`, `.size`, `.empty?` | `Int` / `Bool` | Entry count |
| `.keys`, `.values` | `Array` | Insertion-ordered keys / values |
| `.has_key?(k)`, `.key?(k)` | `Bool` | Membership check |
| `.fetch(k, default?)` | `Any` | Lookup or return default (raises `KeyError` if omitted & missing) |
| `.delete(k)` | `Any` | Remove key and return previous value |
| `.merge(other)` | `Hash` | New hash with entries from `other` overriding |
| `.each { \|k, v\| ... }` | `Hash` | Iterate key/value pairs in insertion order |

### `Range` (`1..5`, `1...5`)
* `.each { |n| ... }`, `.map { |n| ... }`, `.to_a`, `.include?(n)`.

---

## 3. Built-In Functions & Modules

### Global Helpers
* `puts(a, ...)` — prints each argument followed by a newline.
* `print(a, ...)` — prints each argument without a trailing newline and flushes stdout immediately (`io.flush`).
* `eputs(a, ...)` — prints each argument followed by a newline to standard error (`fs.stderr`).
* `gets` — flushes stdout and reads the next line from standard input (`fs.stdin`, without trailing `\n`/`\r\n`), returning `String` or `nil` on EOF.
* `sleep_ms(ms)` — sleeps the current MLWSVM process for `ms` milliseconds.
* `read_only(path)`, `read_write(path)` — capability descriptor constructors for `capabilities fs: ...`.
* `budget(timeout: ..., cost: ..., tokens: ..., turns: ...)` — creates an `AilBudget` for `within`.
* `allow`, `reject(reason)`, `redact(new_value)`, `escalate(reason)` — guardrail verdict constructors.

### `File`, `Files` & `Path`
* `File.read(path)` → `String`
* `File.write(path, text)` → `nil`
* `File.exists?(path)` → `Bool`
* `File.dir?(path)` → `Bool`
* `File.size(path)` → `Int` (bytes)
* `File.list(dir = ".")` → `Array[String]` (sorted)
* `File.cwd` → `String` (current working directory)
* `File.mkdir(path)` / `File.delete(path)` → `nil`
* `Files.glob(pattern = "", dir = ".")` → `Array[String]` (recursive walk, skips `.git`, filters `*.ext`)
* `Path.join(a, b)`, `Path.normalize(p)`, `Path.dirname(p)`, `Path.basename(p)`, `Path.ext(p)` → `String`

### `OS`
* `OS.args` → `Array[String]` (command-line arguments after `--`)
* `OS.platform` → `String` (`"macos"`, `"linux"`, `"windows"`, ...)
* `OS.run(cmd_array)` → `Hash{"status" => Int, "output" => String}` (obeys `cap.restrict` inside sandboxed tools)
* `OS.exit(code = 0)` → exits the VM immediately with `code`

### `JSON`
* `JSON.parse(text)` → AIL `Hash` / `Array`
* `JSON.stringify(val, indent = 0)` → `String`

### Typed Resource Registries (`$llm`, `$s1`, `$embedder`, `$db`, `$vector_db`, `$mcp`, `$skills`, `$a2a`) & `expect config`
* `$llm[:fast]` → `AilResource` pointing to `llm.fast` in `.ail.env`. Valid registries: `$llm`, `$s1`, `$embedder`, `$db`, `$vector_db`, `$mcp`, `$skills`, `$a2a`.
* `$llm[:fast]["model"]`, `$llm[:fast].fetch("model", "unknown")`, `$llm[:fast].to_h` — read fields directly from the configured binding.
* `$llm[:fast].exists?` → `Bool`; `.kind` → `:llm`; `.name` → `:fast`; `.key` → `"llm.fast"`.
* `$db[:sessions] = { driver: "memory" }` — sets (or overwrites) a binding in the VM's shared environment (`env.set`), visible to all agents and spawned processes (`nil` deletes it).
* `expect config do ... errors |errs| ... end` — validates all listed `$kind[:name]` entries (plus optional `driver:` and `capabilities:` constraints). Without `errors`, raises `ConfigError` listing all failures; with `errors |errs|`, each item has `.kind`, `.name`, `.key`, `.reason` so the program can install fallbacks or exit cleanly.

### `Config` & `Env`
* `Config.get(kind, name)` → `Hash` (low-level equivalent of `$kind[:name].to_h`)
* `Config.load(path)` → loads a `.ail.env` file at runtime
* `Env.get(name, default = nil)` → `String` or `nil` (reads OS environment variable via `os.env`)
* `Env.has?(name)` → `Bool`

### `Time`, `Math`, `Random`
* `Time.now` → ISO 8601 UTC timestamp string (e.g. `"2026-11-26T06:45:00Z"`)
* `Time.now_ms` → monotonic milliseconds (`Int`) for elapsed timing
* `Math.sqrt(x)`, `Math.pow(a, b)`, `Math.min(a, b)`, `Math.max(a, b)`
* `Random.int(lo, hi)` (uniform inclusive), `Random.float` (`[0, 1)`), `Random.seed(n)`

---

## 4. AI Runtime Objects

### `Schema`
* `MySchema.new(hash)` / `MySchema.validate(hash)` — validates required fields, applies defaults, returns instance with field dot-accessors (`t.title`). Raises `SchemaError` on missing required fields.
* `MySchema.json_schema` → compiled JSON Schema string.
* `MySchema.name` → `String`.

### `Tool`
* `my_tool(args...)` / `my_tool.call(args...)` — invokes tool (inside an isolated `cap.restrict` process when `capabilities` are declared).
* `my_tool.schema`, `my_tool.name`, `my_tool.doc`.

### `Guard` & `GuardResult`
* `MyGuard.check_input(msg, ctx)`, `MyGuard.check_output(msg, ctx)`, `MyGuard.check_tool(call_hash, ctx)` → `GuardResult`.
* `res.verdict` (`:allow`, `:reject`, `:redact`, `:escalate`), `res.reason`, `res.value`, `res.allowed?`, `res.rejected?`.

### `Context` & `LogView`
* `Context.new(db: :binding, id: "session-1")` (or `Context.new` / `Context.new(:binding)`).
* `ctx.append(kind, payload, meta?)` / `ctx.attach(kind, payload)` — appends to log (and persists to JSONL if configured).
* `ctx.events` → raw `Array` of event hashes.
* `ctx.checkpoint` → `Int` snapshot index; `ctx.rewind(snap)` → truncates to snapshot; `ctx.fork` → independent in-memory copy.
* `ctx.define_view(:name) { |log| ... }` — registers a non-destructive view projection.
* `ctx.view(:name)` → `LogView` (`ctx.view(:audit)` is always lossless).
* **`LogView` methods:** `.drop(:kind)`, `.limit_tokens(n)`, `.compact_before(-n, using: :model)`, `.size`, `.length`, `.to_a`, `.each`, `.map`, `.select`.

### `Agent`
* `MyAgent.run(input, context: ctx, on_token: fn, on_turn: fn, on_tool_call: fn, on_tool_result: fn, on_guard: fn)` — runs full guarded tool/MCP/skill loop.
* `MyAgent.stream(input, context: ctx, on_tool_call: fn, on_tool_result: fn, ...) { |tok| ... }` — streams tokens live via MLWSVM 0.30.0 `std.http.open`/`read_chunk` while firing real-time tool/turn lifecycle hooks:
  - `on_turn: ->(turn_num) { ... }`
  - `on_tool_call: ->(name, args_hash) { ... }`
  - `on_tool_result: ->(name, result_str, ok_bool) { ... }`
  - `on_guard: ->(phase, verdict, detail) { ... }`

### `Flow`
* `MyFlow.run(initial_state, context: ctx?)` — executes stateful graph from `start` node until a terminal node is reached.

### `AilDecideVal` (fields on `decide` results)
* Compares transparently with `==`, `!=`, `<`, `<=`, `>`, `>=` and formats via `.to_s` using its underlying value.
* `.value` (`Symbol` for `choice`, `Int` for `score`, `Float` for `prob`), `.confidence` (`Float` in `0.0..1.0`), `.probs` (`Hash` or `nil`).

### `Knowledge` (RAG / Vector DB)
* `Knowledge.new(vector_db: :kb, embedder: :docs?)` (or `Knowledge.new(:kb)`).
* `kb.ingest(id, text, meta?)` / `kb.add(text, meta?)` — embeds & appends into flat `N × dim` `f32` matrix.
* `kb.search(query, top_k: 5, filter: { ... })` — runs MLWSVM `vec.top_k` over matching rows, returning `Array[{"id", "text", "score", "meta"}]`.
* `kb.size` / `kb.count` → `Int`.

### `MCP` Client
* `cl = MCP.connect($mcp[:binding])` — connects via `stdio` or Streamable HTTP.
* `cl.list_tools` → `Array[Hash]`, `cl.call_tool(:name, args_hash)` → `String`, `cl.read_resource(uri)` → `String`, `cl.close`.

### `A2A` Client & Embedded Server
* `srv = A2A.start(name, handler, host: "127.0.0.1", port: 0, version: "1.0.0", description: "...")` — starts a non-blocking A2A HTTP server in a background MLWSVM process (`port: 0` picks a free ephemeral port). Returns handle with `.url`, `.port`, `.name`, `.stop`.
* `cl = A2A.connect(url_or_binding, timeout_ms: 120000)` — connects to an A2A agent URL or `$a2a[:name]` binding.
* `cl.card` (alias `cl.fetch_card()`) → fetches `GET /.well-known/agent.json` and returns the parsed Agent Card `Hash` (raises `A2AError` on connection, HTTP, or JSON failure).
* `cl.send(text, id: ...)` → sends JSON-RPC 2.0 `message/send` and returns the agent's reply `String` (raises `A2AError` on connection, HTTP, RPC, or task failure).
* `cl.send_task(text, id: ...)`, `cl.get_task(id)`, `cl.cancel(id)` → return full A2A Task `Hash`.

---

## 5. Exception Hierarchy

| Exception Class | Raised When |
|---|---|
| `RuntimeError` | General runtime error or bare `raise "msg"` |
| `TypeError` | Invalid operand types or calling a non-callable |
| `NoMethodError` | Undefined method or function |
| `KeyError` | `Hash#fetch` missing key with no default |
| `SchemaError` | Missing required schema field or invalid structured output |
| `PermissionError` | MLWSVM `cap.restrict` (`E_PERMISSION`) violation |
| `BudgetExceeded` | `within` wall-clock deadline (`quota 6` / `E_QUOTA`), `max_turns`, or `tokens` exceeded |
| `GuardViolation` | Guardrail returned `reject(...)` |
| `GuardEscalation` | Guardrail returned `escalate(...)` |
| `ConfigError` | Missing or invalid `.ail.env` resource binding |
| `LLMError` | HTTP/JSON error from an LLM provider |
| `System1Error` | HTTP/JSON error from a System 1 provider |
| `MCPError` | MCP transport or JSON-RPC error |
| `ToolError` | Tool invocation failure |
| `EmbedderError` / `VectorError` | Embedding API error or dimension mismatch |
| `FlowError` | Invalid flow execution state |

---

## 6. `.ail.env` Configuration Reference

```properties
# Generative LLM (driver: "openai" | "ollama" | "anthropic" | "mock")
# Optional fields for thinking models (e.g. Ollama gemma4/qwen3): "think": false, "reasoning_effort": "none"|"low"|"medium"|"high"
llm.fast = {"driver": "openai", "url": "http://127.0.0.1:11434/v1", "model": "gemma4:31b-mlx", "think": false, "reasoning_effort": "none", "timeout_ms": 120000}
secret llm.claude = {"driver": "anthropic", "model": "claude-sonnet-4-20250514", "api_key": "${ANTHROPIC_API_KEY}"}
llm.test = {"driver": "mock", "reply": "hello"}

# System 1 model (driver: "jev" | "openai" | "mock")
secret s1.router = {"driver": "jev", "url": "https://api.typesafe.ai/v1", "api_key": "${JEV_API_KEY}"}

# Embedder (driver: "openai" | "mock")
embedder.docs = {"driver": "openai", "url": "http://127.0.0.1:11434/v1", "model": "nomic-embed-text"}

# Memory / Context DB (driver: "memory" | "jsonl")
db.sessions = {"driver": "jsonl", "path": "./sessions.jsonl"}

# Vector DB (driver: "memory", metric: "cosine" | "dot" | "l2")
vector_db.kb = {"driver": "memory", "metric": "cosine", "embedder": "docs"}

# External MCP Server (transport: "stdio" | "http")
mcp.local = {"transport": "stdio", "cmd": ["./my-mcp", "--stdio"]}
secret mcp.remote = {"transport": "http", "url": "https://mcp.example.com/mcp", "headers": {"Authorization": "Bearer ${TOKEN}"}}

# Agent Skills Catalog (driver: "dir")
skills.workspace = {"driver": "dir", "paths": ["./skills", "${HOME}/.agents/skills"]}
```

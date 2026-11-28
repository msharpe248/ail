# AIL Class & Type Reference

This is the complete, self-contained reference for every built-in class, value type, module, AI runtime object, and exception available in **AIL** (`v0.5.0`). Everything is described strictly in AIL terms — you do not need to read any VM documentation to write, debug, or ship AIL programs.

---

## Contents

1. [How AIL Uses Its Runtime (In AIL Terms)](#1-how-ail-uses-its-runtime-in-ail-terms)
2. [Universal Object Protocol](#2-universal-object-protocol)
3. [Core Value Classes](#3-core-value-classes) — `Nil`, `Bool`, `Int`, `Float`, `Decimal`, `Duration`, `Symbol`, `String`, `Regex`, `Range`, `Closure`
4. [Collection Classes](#4-collection-classes) — `Array`, `Hash`
5. [User-Defined Classes & Objects](#5-user-defined-classes--objects) — `Class`, Instances
6. [Configuration & Resource Classes](#6-configuration--resource-classes) — `ResourceRegistry`, `Resource`, `ConfigErrorItem`, `Config`
7. [System, I/O & Utility Modules](#7-system-io--utility-modules) — Global Builtins, `File`, `Files`, `Path`, `OS`, `Env`, `JSON`, `Time`, `Math`, `Random`
8. [AI & Protocol Classes](#8-ai--protocol-classes) — `Schema`, `Tool`, `Guard`, `GuardResult`, `Budget`, `Context`, `LogView`, `Agent`, `Flow`, `DecideResult`, `DecideValue`, `Knowledge`, `MCP`, `MCPClient`, `A2A`, `A2AClient`, `A2AHandle`
9. [Exception Classes](#9-exception-classes)

---

## 1. How AIL Uses Its Runtime (In AIL Terms)

AIL compiles to a lightweight, process-isolated virtual machine runtime. As an AIL developer, you never touch VM instructions directly — instead, several runtime guarantees show up automatically in AIL's built-in classes:

- **Lightweight process isolation & sandboxing:**
  - When a `Tool` declares `capabilities` (`fs:`, `net:`, `run:`, `env:`), invoking it runs inside an isolated child process whose OS permissions are restricted to only those grants. Attempting to read, write, connect, or run anything outside those grants raises `PermissionError`.
  - Parallel combinators (`fn_a & fn_b`) run both branches concurrently in isolated processes and return `[result_a, result_b]`.
  - `A2A.start(...)` runs each A2A HTTP server and its task store in background actors so multiple agents can talk over HTTP inside a single program with no shared-memory races.
- **Inherited wall-clock deadlines:**
  - `within budget(timeout: 5.s) do ... end` enforces a real monotonic wall-clock deadline that automatically propagates into every nested function, agent turn, child process, and HTTP call, raising `BudgetExceeded` when time runs out.
- **Shared configuration environment:**
  - Typed resource registries (`$llm`, `$s1`, `$db`, `$vector_db`, `$embedder`, `$mcp`, `$skills`, `$a2a`) read and write a process-safe configuration store loaded from `.ail.env`. Setting a fallback like `$db[:sessions] = { driver: "memory" }` immediately makes it visible to every agent and spawned task.
- **Exact decimal arithmetic & linear-time regexes:**
  - `Decimal` literals (`0.25d`) use exact base-10 arithmetic (so `0.10d + 0.20d == 0.30d`), avoiding floating-point rounding drift in cost accounting.
  - `Regex` literals (`/.../`) are guaranteed to run in time linear in the input length — they are immune to catastrophic backtracking (ReDoS).
- **Fast vector search & incremental streaming:**
  - `Knowledge` stores embeddings in contiguous float memory and runs native top-K similarity scoring (`cosine`, `dot`, or `l2`).
  - `Agent.stream` reads HTTP responses incrementally as chunks arrive, and `print` flushes stdout immediately so streamed tokens appear live.
- **Graceful shutdown & single-binary shipping:**
  - Long-running `mcp_server` and `a2a_agent` services automatically trap `SIGINT`/`SIGTERM` for clean shutdown and `SIGHUP` to hot-reload `.ail.env`.
  - `ail bundle app.ail -o app` packages the runtime and your program into a single standalone executable.

---

## 2. Universal Object Protocol

Every value in AIL — primitives, collections, user class instances, and AI runtime objects — supports:

| Method / Rule | Returns | Description |
|---|---|---|
| `.to_s` | `String` | Human-readable string representation (also used inside `"#{expr}"` interpolation and `puts`/`print`) |
| `.inspect` | `String` | Developer/REPL representation (e.g., `"hi"` quoted, `:sym` prefixed, `0.25d` suffixed, `$llm[:fast]`) |
| `.nil?` | `Bool` | `true` only for `nil`; `false` for every other value |
| `==`, `!=` | `Bool` | Value equality across numbers, strings, symbols, booleans, decimals, resources, and `DecideValue`s |
| Truthiness | — | **Only `nil` and `false` are falsy.** `0`, `""`, `[]`, and `{}` are all truthy (Ruby semantics). |

---

## 3. Core Value Classes

### `Nil` (`nil`)
Represents the absence of a value.
- `.nil?` → `true`
- `.to_s` → `""`
- `.inspect` → `"nil"`

### `Bool` (`true`, `false`)
Boolean values produced by comparisons, predicates (`?` methods), and logical operators (`&&`, `||`, `!`, `and`, `or`, `not`).
- `.to_s` / `.inspect` → `"true"` or `"false"`

### `Int`
64-bit signed integer (`42`, `-7`, `1_000_000`, `0xFF`).
- **Operators:** `+`, `-`, `*`, `/`, `%`, unary `-`, `<`, `<=`, `>`, `>=`, `==`, `!=`. Mixing `Int` with `Float` promotes to `Float`; mixing `Int` with `Decimal` promotes to `Decimal`.

| Method | Returns | Description |
|---|---|---|
| `.abs` | `Int` | Absolute value |
| `.even?` / `.odd?` | `Bool` | Parity predicates |
| `.times { \|i\| ... }` | `Int` | Calls block `n` times with `i = 0, 1, ..., n - 1` |
| `.ms` / `.s` / `.m` | `Duration` | Converts integer to milliseconds, seconds, or minutes (e.g., `500.ms`, `30.s`, `2.m`) |
| `.to_i` | `Int` | Returns itself |
| `.to_f` | `Float` | Converts to floating-point |

```ruby
3.times { |i| print "#{i} " }   # => 0 1 2
timeout = 30.s                  # => Duration(30000ms)
```

### `Float`
64-bit IEEE-754 floating-point number (`3.14`, `1.5e-3`).
- **Operators:** `+`, `-`, `*`, `/`, `%`, unary `-`, `<`, `<=`, `>`, `>=`, `==`, `!=`.

| Method | Returns | Description |
|---|---|---|
| `.s` | `Duration` | Converts fractional seconds to a `Duration` (e.g., `1.5.s` → `1500ms`) |
| `.to_i` | `Int` | Truncates toward zero |
| `.to_f` | `Float` | Returns itself |

### `Decimal`
Exact base-10 decimal (`0.10d`, `19.99d`), backed by the runtime's arbitrary-precision decimal type. Ideal for financial amounts and LLM cost budgets.
- **Operators:** `+`, `-`, `*`, `/`, unary `-`, `<`, `<=`, `>`, `>=`, `==`, `!=`.

| Method | Returns | Description |
|---|---|---|
| `.round(places = 0)` | `Decimal` | Rounds to `places` decimal digits (half-even rounding) |
| `.to_f` | `Float` | Converts to 64-bit float |
| `.to_s` / `.inspect` | `String` | `"0.30"` / `"0.30d"` |

```ruby
total = 0.10d + 0.20d
total == 0.30d        # => true
total.round(1)        # => 0.3d
```

### `Duration`
Represents a span of time in milliseconds. Created via `Int#ms`, `Int#s`, `Int#m`, or `Float#s`, and used by `budget(timeout: ...)`.

| Method | Returns | Description |
|---|---|---|
| `.ms` | `Int` | Total duration in milliseconds |
| `.to_s` | `String` | Formatted duration (e.g. `"5000ms"`) |

### `Symbol`
Immutable interned identifier (`:billing`, `:tools`, `:prompt_injection?`). Guaranteed O(1) identity equality.

| Method | Returns | Description |
|---|---|---|
| `.to_s` | `String` | Name without leading colon (`:billing.to_s` → `"billing"`) |
| `.inspect` | `String` | With leading colon (`":billing"`) |
| `.to_sym` | `Symbol` | Returns itself |

### `String`
Immutable UTF-8 text. Supports:
- Double-quoted strings with interpolation (`"Hi #{name}"`) and escapes (`\n`, `\r`, `\t`, `\e`, `\xHH`, `\0`, `\\`, `\"`, `\#`)
- Single-quoted raw strings (`'no #{interp}'`)
- Dedented heredocs (`<<~PROMPT ... PROMPT`)
- **Operators:** `a + b` (concatenation), `s * n` (repeat `n` times), `s[i]` (byte/char index; negative indexes from end), `s =~ /re/` and `s !~ /re/` (regex match), `<`, `<=`, `>`, `>=`, `==`, `!=`.

| Method | Returns | Description |
|---|---|---|
| `.length`, `.size` | `Int` | Length in bytes |
| `.empty?` | `Bool` | `true` if length is 0 |
| `.upcase` / `.downcase` | `String` | Full Unicode case conversion |
| `.strip` / `.trim` | `String` | Removes leading and trailing Unicode whitespace |
| `.chomp` | `String` | Strips a single trailing `\r\n` or `\n` |
| `.split(sep = "")` | `Array[String]` | Splits on `sep` (default: runs of whitespace) |
| `.lines` | `Array[String]` | Splits into lines (strips `\n` and `\r\n`) |
| `.chars` | `Array[String]` | Extended grapheme clusters (user-perceived characters) |
| `.slice(start, len)` | `String` | Substring of `len` bytes starting at `start` |
| `.include?(sub)` | `Bool` | Substring containment check |
| `.start_with?(prefix)` / `.end_with?(suffix)` | `Bool` | Prefix / suffix check |
| `.match?(pattern)` | `Bool` | Tests `Regex` (or pattern `String`) anywhere in the string |
| `.sub(pattern, repl)` | `String` | Replaces the **first** occurrence (`String` or `Regex`) |
| `.gsub(pattern, repl)` / `.replace(pattern, repl)` | `String` | Replaces **all** occurrences (`String` or `Regex`) |
| `.to_i` / `.to_f` | `Int` / `Float` | Parses numeric value (returns `0` / `0.0` if non-numeric) |

### `Regex`
Compiled linear-time regular expression literal (`/\b\d{3}-\d{2}-\d{4}\b/i`). Supported flags: `i` (case-insensitive), `m` (multiline `^`/`$`), `s` (`.` matches newline).
- Used with `String#=~`, `String#!~`, `String#match?`, `String#sub`, and `String#gsub`.

### `Range`
Integer interval created with `a..b` (inclusive) or `a...b` (exclusive of `b`).

| Method | Returns | Description |
|---|---|---|
| `.each { \|n\| ... }` | `Range` | Yields each integer in the range |
| `.map { \|n\| ... }` | `Array` | Maps each integer through block |
| `.to_a` | `Array[Int]` | Materializes range into an array |
| `.include?(n)` | `Bool` | `true` if integer `n` falls within the range |

### `Closure` (Blocks & Lambdas)
First-class callable functions created via lambdas (`->(x, y) { x + y }`) or blocks (`do |x| ... end` / `{ |x| ... }`). Closures capture their lexical scope and can read/mutate enclosing variables.
- **Combinator operators:**
  - `f --> g` — sequential pipeline closure (`g.call(f.call(x))`)
  - `f | g` — fallback closure (calls `g` if `f` raises an exception)
  - `f & g` — parallel fan-out closure (runs `f` and `g` concurrently in isolated processes, returns `[res_f, res_g]`)

| Method | Returns | Description |
|---|---|---|
| `.call(args...)` | `Any` | Invokes the closure with arguments |

---

## 4. Collection Classes

### `Array`
Mutable, dynamically resized ordered list (`[1, "two", :three]`).
- **Operators:** `a[i]` (negative indexes count from end; out-of-bounds returns `nil`), `a[i] = val`, `a + b` (returns new concatenated array).

| Method | Returns | Description |
|---|---|---|
| `.length`, `.size`, `.count` | `Int` | Number of elements |
| `.empty?` | `Bool` | `true` if size is 0 |
| `.first` / `.last` | `Any` / `nil` | First or last element |
| `.push(val)` | `Array` | Appends `val` in place and returns `self` |
| `.pop` | `Any` / `nil` | Removes and returns last element |
| `.join(sep = "")` | `String` | Joins stringified elements with `sep` |
| `.reverse` | `Array` | New array in reverse order |
| `.include?(val)` | `Bool` | Value equality membership check |
| `.take(n)` / `.drop(n)` | `Array` | First `n` elements / all elements after first `n` |
| `.compact` | `Array` | New array with all `nil` elements removed |
| `.uniq` | `Array` | New array with duplicates removed (preserves first order) |
| `.flatten` | `Array` | Flattens one level of nested arrays |
| `.sort` | `Array` | New array sorted ascending (numbers/strings) |
| `.each { \|x\| ... }` | `Array` | Iterates each element |
| `.each_with_index { \|x, i\| ... }` | `Array` | Iterates element and 0-based index |
| `.map { \|x\| ... }` | `Array` | Transforms each element |
| `.flat_map { \|x\| ... }` | `Array` | Maps and flattens one level |
| `.select { \|x\| ... }` / `.filter` | `Array` | Elements where block returns truthy |
| `.reject { \|x\| ... }` | `Array` | Elements where block returns falsy |
| `.reduce(init) { \|acc, x\| ... }` / `.inject` | `Any` | Left fold starting from `init` |
| `.any? { \|x\| ... }` / `.all? { \|x\| ... }` | `Bool` | Short-circuiting existential / universal check |
| `.find { \|x\| ... }` | `Any` / `nil` | First matching element |
| `.group_by { \|x\| ... }` | `Hash` | Groups elements into `{ key => [items...] }` |

### `Hash`
Mutable, insertion-ordered key-value map (`{ a: 1, "b" => 2 }`). Symbol keys and string keys normalize to strings on lookup (`h[:a]` and `h["a"]` access the same entry).
- **Indexing & dot access:** `h[k]` (returns `nil` if absent), `h[k] = val`, and `h.foo` (dot-access falls back to key `"foo"` when no built-in `Hash` method matches).

| Method | Returns | Description |
|---|---|---|
| `.length`, `.size` | `Int` | Number of entries |
| `.empty?` | `Bool` | `true` if no entries |
| `.keys` / `.values` | `Array` | Insertion-ordered keys / values |
| `.has_key?(k)` / `.key?(k)` | `Bool` | Membership check |
| `.fetch(k, default?)` | `Any` | Returns value for `k`, or `default` if given; raises `KeyError` if missing with no default |
| `.delete(k)` | `Any` / `nil` | Removes key `k` and returns its previous value |
| `.merge(other)` | `Hash` | Returns a new hash combining `self` and `other` (`other` wins on key collisions) |
| `.each { \|k, v\| ... }` | `Hash` | Iterates key/value pairs in insertion order |

---

## 5. User-Defined Classes & Objects

Define custom classes with `class Name < Parent` (single inheritance), instance variables (`@field`), constructor `initialize`, and `self`:

```ruby
class Counter
  def initialize(start = 0)
    @count = start
  end

  def inc(by = 1)
    @count = @count + by
    self
  end

  def value
    @count
  end
end

c = Counter.new(10)
c.inc(5).value   # => 15
```

- **Class methods:** `MyClass.new(args...)` (allocates instance and runs `initialize` if defined), `MyClass.name` → `String`.
- **Instance fields:** Accessed via `@ivar` inside methods, `obj[key]` / `obj[key] = val`, or dot-access if a field exists on the object.

---

## 6. Configuration & Resource Classes

### `ResourceRegistry` (`$llm`, `$s1`, `$embedder`, `$db`, `$vector_db`, `$mcp`, `$skills`, `$a2a`)
The 8 built-in typed global registries that map 1-to-1 to keys in `.ail.env` (`$llm[:fast]` ↔ `llm.fast`). Accessing an unknown registry name raises `ConfigError`.

| Operation | Returns | Description |
|---|---|---|
| `$kind[:name]` | `Resource` | Returns a typed `Resource` handle referencing `<kind>.<name>` |
| `$kind[:name] = hash` | `Hash` | Sets or overrides `<kind>.<name>` in the shared runtime environment (immediately visible to all agents and child processes) |
| `$kind[:name] = nil` | `nil` | Removes `<kind>.<name>` from the runtime environment |

### `Resource` (e.g., `$llm[:fast]`, `$db[:sessions]`, `$a2a[:vega]`)
First-class handle representing a configured external resource. Pass directly to `agent`, `decide`, `Context.new`, `Knowledge.new`, `MCP.connect`, `A2A.connect`, `expect config`, or inspect its config fields directly:

| Operation / Method | Returns | Description |
|---|---|---|
| `res["field"]` | `Any` / `nil` | Reads a field directly from the bound JSON config (e.g. `$llm[:fast]["model"]`) |
| `.kind` | `Symbol` | Registry kind (`:llm`, `:db`, `:a2a`, ...) |
| `.name` | `Symbol` | Binding name (`:fast`, `:sessions`, ...) |
| `.key` | `String` | Full `.ail.env` key (`"llm.fast"`) |
| `.exists?` | `Bool` | `true` if currently configured in the runtime environment |
| `.to_h` / `.config` | `Hash` | Full configuration hash (empty `{}` if unconfigured) |
| `.fetch(k, def?)`, `.has_key?(k)`, `.keys`, `.field` | `Any` | Delegates directly to the underlying config `Hash` |
| `==`, `!=` | `Bool` | Compares both kind and name (`$llm[:fast] == $llm[:fast]`) |

### `ConfigErrorItem` (inside `expect config ... errors |errs|`)
Each element in `errs` passed to an `expect config` error handler is a `Hash` supporting dot-access:
- `e.kind` → `Symbol` (`:llm`, `:db`, ...)
- `e.name` → `Symbol` (`:fast`, `:sessions`, ...)
- `e.key` → `String` (`"llm.fast"`)
- `e.reason` → `String` (e.g. `"not configured in .ail.env"`, `"expected driver '...', got '...'"`, `"missing required capability ':tools'"`)

### `Config`
Low-level configuration helper module (prefer `$kind[:name]` for normal lookups):
- `Config.get(kind, name)` → `Hash` (same as `$kind[:name].to_h`)
- `Config.load(path)` → `nil` (loads an additional `.ail.env` file at runtime)

---

## 7. System, I/O & Utility Modules

### Global Built-In Functions
| Function | Returns | Description |
|---|---|---|
| `puts(args...)` | `nil` | Prints each argument followed by a newline to stdout |
| `print(args...)` | `nil` | Prints each argument without a newline and flushes stdout immediately |
| `eputs(args...)` | `nil` | Prints each argument followed by a newline to stderr |
| `gets` | `String` / `nil` | Flushes stdout and reads one line from stdin (without trailing `\r\n`/`\n`); returns `nil` on EOF |
| `sleep_ms(ms)` | `nil` | Sleeps the current process for `ms` milliseconds |
| `budget(timeout:, cost:, tokens:, turns:)` | `Budget` | Constructs a budget specification for `within budget(...) do ... end` |
| `read_only(path)` / `read_write(path)` | `Hash` | Filesystem capability grant descriptors for `capabilities fs: ...` |
| `allow` / `reject(reason)` / `redact(val)` / `escalate(reason)` | `GuardResult` | Verdicts returned from `guard` hooks |

### `File` & `Files`
All file operations automatically respect `capabilities fs:` restrictions when executed inside sandboxed tools (raising `PermissionError` if disallowed).

| Method | Returns | Description |
|---|---|---|
| `File.read(path)` | `String` | Reads entire file as UTF-8 text |
| `File.write(path, text)` | `nil` | Creates or overwrites file with `text` |
| `File.exists?(path)` | `Bool` | `true` if path exists |
| `File.dir?(path)` | `Bool` | `true` if path exists and is a directory |
| `File.size(path)` | `Int` | File size in bytes |
| `File.list(dir = ".")` | `Array[String]` | Sorted entry names in `dir` |
| `File.cwd` | `String` | Current working directory |
| `File.mkdir(path)` | `nil` | Creates directory |
| `File.delete(path)` | `nil` | Removes file |
| `Files.glob(pattern = "", dir = ".")` | `Array[String]` | Recursively lists files under `dir` (skips `.git`; supports extension filter like `"*.ail"`) |

### `Path`
| Method | Returns | Description |
|---|---|---|
| `Path.join(a, b)` | `String` | Joins path segments |
| `Path.normalize(p)` | `String` | Cleans `.`, `..`, and duplicate separators |
| `Path.dirname(p)` | `String` | Parent directory |
| `Path.basename(p)` | `String` | Final path component |
| `Path.ext(p)` | `String` | File extension including dot (e.g. `".ail"`) |

### `OS`
| Method | Returns | Description |
|---|---|---|
| `OS.args` | `Array[String]` | Command-line arguments passed after `--` |
| `OS.platform` | `String` | Operating system name (`"macos"`, `"linux"`, `"windows"`, ...) |
| `OS.run(cmd_array)` | `Hash` | Runs command array (e.g. `OS.run(["git", "status", "-s"])`) returning `{ "status" => Int, "output" => String }`. Respects `capabilities run:` inside sandboxed tools. |
| `OS.exit(code = 0)` | — | Exits the program immediately with status `code` |

### `Env`
| Method | Returns | Description |
|---|---|---|
| `Env.get(name, default = nil)` | `String` / `nil` | Reads an OS environment variable |
| `Env.has?(name)` | `Bool` | `true` if OS environment variable is set |

### `JSON`
| Method | Returns | Description |
|---|---|---|
| `JSON.parse(str)` | `Hash` / `Array` | Parses a JSON object or array into AIL collections/primitives |
| `JSON.stringify(val, indent = 0)` | `String` | Serializes AIL values to JSON (`indent: 0` is compact, `indent: 2` is pretty-printed) |

### `Time`
| Method | Returns | Description |
|---|---|---|
| `Time.now` | `String` | Current UTC wall-clock timestamp in ISO 8601 (`"2026-11-27T23:00:00Z"`) |
| `Time.now_ms` | `Int` | Monotonic clock in milliseconds (for measuring elapsed durations) |

### `Math`
| Method | Returns | Description |
|---|---|---|
| `Math.sqrt(x)` | `Float` | Square root |
| `Math.pow(a, b)` | `Float` | `a` raised to the power `b` |
| `Math.min(a, b)` / `Math.max(a, b)` | `Int` / `Float` | Smaller / larger of two comparable values |

### `Random`
| Method | Returns | Description |
|---|---|---|
| `Random.int(lo, hi)` | `Int` | Uniform random integer in `[lo, hi]` (inclusive) |
| `Random.float` | `Float` | Uniform random float in `[0.0, 1.0)` |
| `Random.seed(n)` | `nil` | Seeds the current process's PRNG for deterministic runs |

---

## 8. AI & Protocol Classes

### `Schema` & Schema Instances
Created with `schema Name ... end`. Generates a compile-time JSON Schema and validates fields at runtime.

| Method | Returns | Description |
|---|---|---|
| `MySchema.new(hash)` / `MySchema.validate(hash)` | Instance | Validates required fields, applies defaults, returns record instance; raises `SchemaError` if a required field is missing |
| `MySchema.json_schema` | `String` | Compile-time generated JSON Schema string |
| `MySchema.name` | `String` | Schema name |
| `inst.field_name` / `inst["field_name"]` | `Any` | Accesses validated fields on an instance |

### `Tool`
Created with `tool :name, "doc" do ... end`. Callable directly (`my_tool(a, b)`) or exposed to `Agent`s and `mcp_server`s. If `capabilities` are declared, every call executes inside an isolated sandboxed process.

| Method | Returns | Description |
|---|---|---|
| `my_tool(args...)` / `.call(args...)` | `Any` | Invokes tool (accepts positional args or named argument hash, applying parameter defaults) |
| `.name` | `String` | Tool name |
| `.doc` | `String` | Tool description |
| `.schema` | `String` | Generated JSON Schema for the tool's parameters |

### `Guard` & `GuardResult`
Created with `guard Name do check_input ... check_output ... check_tool ... end`.

| Method on `Guard` | Returns | Description |
|---|---|---|
| `.check_input(msg, ctx?)` | `GuardResult` | Runs `check_input` hook |
| `.check_output(msg, ctx?)` | `GuardResult` | Runs `check_output` hook |
| `.check_tool(call_hash, ctx?)` | `GuardResult` | Runs `check_tool` hook (`call_hash` has `"name"` and `"arguments"`) |
| `.name` | `String` | Guard name |

| Method on `GuardResult` | Returns | Description |
|---|---|---|
| `.verdict` | `Symbol` | `:allow`, `:reject`, `:redact`, or `:escalate` |
| `.reason` | `String` | Rejection or escalation message |
| `.value` | `Any` | Replacement value when verdict is `:redact` |
| `.allowed?` / `.rejected?` | `Bool` | Convenience predicates |

### `Budget`
Created with `budget(timeout: 5.s, cost: 0.10d, tokens: 8000, turns: 6)` and passed to `within`:

```ruby
result = within budget(timeout: 5.s) do
  MyAgent.run(prompt)
rescue BudgetExceeded => e
  "Timed out: #{e.message}"
end
```

### `Context` & `LogView`
Append-only, lossless conversation and event log with composable non-destructive view projections and optional JSONL persistence.

**Constructors:** `Context.new`, `Context.new($db[:sessions])`, `Context.new(db: $db[:sessions], id: "session-1")`.

| Method on `Context` | Returns | Description |
|---|---|---|
| `.append(kind, payload, meta?)` / `.attach(kind, payload)` | `Context` | Appends an event (`{ "kind", "payload", "meta"? }`) and persists to JSONL if DB-backed |
| `.events` | `Array[Hash]` | Raw list of all recorded events |
| `.checkpoint` | `Int` | Returns current event-log length as a rewind token |
| `.rewind(snap)` | `Context` | Truncates the log back to `snap` (and rewrites JSONL file if persisted) |
| `.fork` | `Context` | Returns an independent in-memory copy (events and views) |
| `.define_view(:name) { \|log\| ... }` | `Context` | Registers a named projection over the event log |
| `.view(:name)` | `LogView` | Projects the named view (`:audit` always returns the full lossless log) |

| Method on `LogView` | Returns | Description |
|---|---|---|
| `.drop(:kind)` | `LogView` | Excludes events matching `kind` (e.g. `:tool_intermediate`) |
| `.compact_before(neg_idx, using: $llm[:fast])` | `LogView` | Summarizes older events before `neg_idx` into a single `:summary` event |
| `.limit_tokens(max_tokens)` | `LogView` | Keeps the most recent events fitting within an approximate token budget |
| `.size` / `.length` | `Int` | Event count in this view |
| `.to_a` | `Array[Hash]` | Materializes view as an array of event hashes |
| `.each`, `.map`, `.select` | `LogView` / `Array` | Standard internal iterators over the projected events |

### `Agent`
Created with `agent Name do ... end`. Runs an autonomous multi-turn loop over a configured `$llm[:...]`, local `Tool`s, external `$mcp[:...]` servers, `$skills[:...]` / inline `skill`s (with progressive disclosure via built-in `load_skill`), `Guard`s, `Context` views, token/turn `limits`, and optional structured `returns Schema`.

| Method | Returns | Description |
|---|---|---|
| `.run(input, opts?)` | `String` / Schema Instance | Runs the full agent loop and returns the final answer (or validated `Schema` instance if `returns` is set) |
| `.stream(input, opts?) { \|tok\| ... }` | `String` / Schema Instance | Same as `.run`, streaming text tokens incrementally into the block as they arrive |

**Supported `opts` keyword arguments:**
- `context: ctx` — `Context` instance (creates a fresh one if omitted)
- `on_token: ->(tok) { ... }` — token callback (automatically wired when a block is passed to `.stream`)
- `on_turn: ->(turn_num) { ... }` — called at the start of each LLM turn
- `on_tool_call: ->(name, args_hash) { ... }` — called right before executing a tool call
- `on_tool_result: ->(name, result_str, ok_bool) { ... }` — called immediately after a tool succeeds, errors, or is blocked by a guard
- `on_guard: ->(phase, verdict, detail) { ... }` — called when a guard blocks a tool call
- `timeout_ms: n` / `temperature: f` — per-call overrides forwarded to the LLM driver

### `Flow`
Created with `flow Name ... end`. Statically verified at compile time for node existence, reachability, and bounded cycles (`max: N`).

| Method | Returns | Description |
|---|---|---|
| `MyFlow.run(initial_state, context: ctx?)` | `Any` | Executes graph from `start`, evaluating `node`, `decide`, and conditional `edge` (`when:`/`unless:`/`max:`) transitions until a terminal node completes |

### `DecideResult` & `DecideValue`
Returned by System 1 `decide using: $s1[:router], on: input do ... end`.
- `DecideResult` exposes each declared `choice`, `score`, and `prob` field as a dot-accessor (`d.queue`, `d.urgency`, `d.duplicate?`), returning a `DecideValue`.
- `DecideValue` compares and formats **transparently** as its underlying value (`d.queue == :billing`, `d.urgency >= 4`, `"#{d.queue}"` → `"billing"`), while also exposing calibrated metadata:

| Method on `DecideValue` | Returns | Description |
|---|---|---|
| `.value` | `Symbol` / `Int` / `Float` | Underlying decision value (`Symbol` for `choice`, `Int` for `score`, `Float` for `prob`) |
| `.confidence` | `Float` | Calibrated confidence in `0.0..1.0` |
| `.probs` | `Hash` / `nil` | Optional probability distribution across options |

### `Knowledge`
Pluggable Retrieval-Augmented Generation (RAG) vector store backed by a configured `$vector_db[:...]` and `$embedder[:...]`.

**Constructors:** `Knowledge.new(vector_db: $vector_db[:kb], embedder: $embedder[:docs]?)` or `Knowledge.new($vector_db[:kb])`.

| Method | Returns | Description |
|---|---|---|
| `.ingest(id, text, meta?)` | `Knowledge` | Embeds `text` and stores document with `id` and optional metadata `Hash` |
| `.add(text, meta?)` | `Knowledge` | Same as `.ingest` with an auto-generated ID |
| `.search(query, top_k: 5, filter: { ... }?)` | `Array[Hash]` | Returns up to `top_k` matches sorted best-first, each `{ "id", "text", "score", "meta" }`; optional `filter` pre-filters rows by exact metadata equality |
| `.size` / `.count` | `Int` | Number of ingested documents |

### `MCP` & `MCPClient`
Client for connecting programmatically to external MCP servers (`stdio` or Streamable HTTP).

| Method | Returns | Description |
|---|---|---|
| `MCP.connect($mcp[:name])` | `MCPClient` | Connects and performs MCP `initialize` handshake |
| `cl.list_tools` | `Array[Hash]` | Lists tools advertised by the server |
| `cl.call_tool(:name, args_hash)` | `String` | Invokes a remote MCP tool |
| `cl.read_resource(uri)` | `String` | Reads a remote MCP resource |
| `cl.close` | `nil` | Closes transport / terminates stdio subprocess |

### `A2A`, `A2AClient` & `A2AHandle`
Client and embedded server API for Agent-to-Agent (A2A) HTTP communication (`/.well-known/agent.json` Agent Cards + JSON-RPC 2.0 `message/send`, `tasks/get`, `tasks/cancel`).

| Method | Returns | Description |
|---|---|---|
| `A2A.start(name, handler, host: "127.0.0.1", port: 0, version: "1.0.0", description: "")` | `A2AHandle` | Starts a non-blocking A2A HTTP server in a background process (`port: 0` picks a free port). `handler` may be an `Agent`, `Flow`, `Tool`, or `Closure`. |
| `A2A.connect(url_or_res, timeout_ms: 120000)` | `A2AClient` | Connects to an A2A URL string or `$a2a[:name]` binding |
| `handle.url` / `handle.port` / `handle.name` | `String` / `Int` / `String` | Bound URL, port number, and agent name |
| `handle.stop` | `nil` | Shuts down the background A2A HTTP server and its task store |
| `cl.url` | `String` | Base URL of the connected A2A agent |
| `cl.card` (alias `cl.fetch_card()`) | `Hash` | Fetches `GET /.well-known/agent.json` and returns parsed Agent Card (`name`, `description`, `version`, `url`, `capabilities`, `skills`); raises `A2AError` on failure |
| `cl.send(text, id: ...)` | `String` | Sends JSON-RPC `message/send` and returns the agent's text reply; raises `A2AError` on connection, HTTP, RPC, or task failure |
| `cl.send_task(text, id: ...)` | `Hash` | Sends `message/send` and returns the full A2A Task object |
| `cl.get_task(id)` | `Hash` | Fetches task status/result via `tasks/get` |
| `cl.cancel(id)` | `Hash` | Cancels a task via `tasks/cancel` |

---

## 9. Exception Classes

All AIL exceptions (and runtime errors such as sandbox denials or expired deadlines) are caught with `begin ... rescue ExceptionClass => e ... ensure ... end`. Every caught exception exposes:
- `e.message` → `String`
- `e.class_name` → `String`

Catch-all base classes: `StandardError` and `Exception` match any raised error (or omit the class name: `rescue => e`).

| Exception Class | Raised When |
|---|---|
| `RuntimeError` | General runtime failure or bare `raise "message"` |
| `TypeError` | Invalid operand types or calling a non-callable value |
| `NoMethodError` | Calling an undefined method or function |
| `KeyError` | `Hash#fetch(k)` when `k` is missing and no default was supplied |
| `SchemaError` | Missing required `schema` field or invalid structured agent output |
| `PermissionError` | Attempted file, network, command, or env access outside a tool's `capabilities` sandbox |
| `BudgetExceeded` | `within budget(timeout: ...)` wall-clock deadline expired, or agent exceeded `max_turns` / `tokens` |
| `GuardViolation` | A `guard` hook returned `reject(reason)` |
| `GuardEscalation` | A `guard` hook returned `escalate(reason)` |
| `ConfigError` | Unknown `$registry`, missing `.ail.env` binding, or failed `expect config` without an `errors` handler |
| `LLMError` | HTTP, network, or JSON error communicating with an LLM provider |
| `System1Error` | HTTP, network, or JSON error communicating with a System 1 provider |
| `MCPError` | MCP transport or JSON-RPC failure |
| `A2AError` | A2A connection, HTTP, JSON, JSON-RPC, or failed-task error |
| `ToolError` | Tool invocation failure |
| `EmbedderError` / `VectorError` | Embedding API failure or vector dimension mismatch |
| `FlowError` | Invalid flow runtime state |

# Support Desk — Worked Multi-File AIL Application

A complete multi-file AIL application demonstrating how real-world AI services are structured with `require`, shared domain `schema`s, VM-sandboxed `tool`s, safety `guard`s, `Knowledge` RAG, System 1 `decide`, a stateful `flow`, and an autonomous `agent`.

## File Layout

| File | Responsibility |
|---|---|
| [`schemas.ail`](schemas.ail) | Shared `Ticket` and `Resolution` schemas with compile-time JSON Schema generation and runtime validation |
| [`guards.ail`](guards.ail) | `PIIGuard` — rejects prompt injections on input and redacts SSNs on output |
| [`tools.ail`](tools.ail) | `lookup_policy` tool with zero-privilege `capabilities` (`fs: :none, net: :none, run: :none`), plus a diamond `require "./schemas"` |
| [`main.ail`](main.ail) | Wires everything together: seeds `Knowledge` RAG, defines `DeskAgent` and `SupportDeskFlow`, validates a `Ticket`, routes it via System 1 `decide`, retrieves KB docs, runs the agent, and exercises `v0.5.0` stdlib helpers |
| [`mock.ail.env`](mock.ail.env) | Deterministic offline driver bindings (used by `make test`) |
| [`ollama.ail.env`](ollama.ail.env) | Live local Ollama bindings (`gemma4:31b-mlx`) — same `.ail` code, zero edits |

## Running

From the repository root:

```sh
# Static check (verifies all required files, schemas, and flow graph)
./ail check examples/support_desk/main.ail

# Run deterministically offline
./ail run examples/support_desk/main.ail --env examples/support_desk/mock.ail.env

# Run live against local Ollama
./ail run examples/support_desk/main.ail --env examples/support_desk/ollama.ail.env
```

Expected offline output:

```text
ticket=T-2026-01:tags=billing,urgent
resolution=T-2026-01:billing:esc=false
reply=Refund initiated (3 business days). Ref [REDACTED-SSN].
stdlib=g2:m20:99:hello AIL world:sqrttrue:max9:ttrue
```

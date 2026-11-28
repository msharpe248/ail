# 3-Agent A2A Texas Hold'em Poker (`examples/a2a_poker/`)

Three autonomous AI poker players — **Vega "The Shark"**, **"Lucky Lou"**, and **Professor Bayes** — each running as a live **A2A (Agent-to-Agent) HTTP server** (`GET /.well-known/agent.json` + JSON-RPC 2.0 `POST /` `message/send`), playing No-Limit Texas Hold'em against each other.

## How It Works

1. **[`table.ail`](table.ail)** validates `$llm[:poker]` using `expect config do ... errors |errs| ... end` (and automatically defaults per-seat `$llm[:vega]`, `$llm[:lou]`, `$llm[:bayes]` to `$llm[:poker]` if you don't override them with separate models!).
2. Each player agent from **[`players.ail`](players.ail)** is launched as an independent A2A HTTP server via `A2A.start(...)` on an ephemeral localhost port (or connects to external A2A servers if `$a2a[:vega]` / `$a2a[:lou]` / `$a2a[:bayes]` are configured in `.ail.env`).
3. The dealer connects via `A2A.connect(url)`, fetches each player's **A2A Agent Card (`/.well-known/agent.json`)**, and runs a complete **No-Limit Texas Hold'em** match (Pre-Flop, Flop, Turn, River, Showdown) using the 7-card hand evaluator in **[`cards.ail`](cards.ail)**.
4. On every decision, the dealer sends only that player's private hole cards + public board/pot state over real HTTP A2A `message/send` and receives a JSON action + table talk.

## Running

From the repository root:

```sh
# Watch 3 Ollama agents play 3 hands of Texas Hold'em live!
./ail run examples/a2a_poker/table.ail --env examples/a2a_poker/ollama.ail.env

# Play 5 hands
./ail run examples/a2a_poker/table.ail --env examples/a2a_poker/ollama.ail.env -- 5

# Deterministic offline test (mock agents over real local A2A HTTP servers)
AIL_POKER_SEED=42 ./ail run examples/a2a_poker/table.ail --env examples/a2a_poker/mock.ail.env -- 2
```

### Tip: Pit 3 Different Models Against Each Other!
Edit [`ollama.ail.env`](ollama.ail.env) to assign different Ollama (or OpenAI/Anthropic) models to each seat:
```properties
llm.vega  = {"driver": "openai", "url": "http://127.0.0.1:11434/v1", "model": "gemma4:31b-mlx", "think": false}
llm.lou   = {"driver": "openai", "url": "http://127.0.0.1:11434/v1", "model": "qwen3:8b", "think": false}
llm.bayes = {"driver": "openai", "url": "http://127.0.0.1:11434/v1", "model": "llama3.1:8b"}
```

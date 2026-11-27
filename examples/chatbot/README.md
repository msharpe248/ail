# Interactive Streaming Chatbot (`examples/chatbot/`)

A clean, minimal interactive chatbot loop written in AIL featuring:
- Live token-by-token streaming via `ChatBot.stream(...) { |tok| print tok }`
- Persistent in-session memory via append-only `Context` with a token-bounded `:window` view
- Time-travel commands (`/checkpoint`, `/rewind`) and audit inspection (`/history`)
- Output PII redaction via `ChatGuard`

## Running

From the repository root:

```sh
# Interactive chat against local Ollama
./ail run examples/chatbot/main.ail --env examples/chatbot/ollama.ail.env

# Offline deterministic demo
printf "Hi there!\nDo you remember me?\n/history\n/quit\n" | \
  ./ail run examples/chatbot/main.ail --env examples/chatbot/mock.ail.env
```

## Slash Commands

| Command | Action |
|---|---|
| `/help` | Show available commands |
| `/history` | Print all events in the lossless `:audit` log |
| `/checkpoint` | Save a point-in-time checkpoint of the conversation |
| `/rewind` | Rewind the conversation back to the saved checkpoint |
| `/reset` | Clear context and start fresh |
| `/quit` / `/exit` | Exit the chatbot |

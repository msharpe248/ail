#!/bin/sh
set -eu

AIL=./ail
PASS=0
FAIL=0

pass() {
    echo "PASS: $1"
    PASS=$((PASS + 1))
}

fail() {
    echo "FAIL: $1"
    FAIL=$((FAIL + 1))
    exit 1
}

echo "=== 1. Compiler checks (ail check) ==="
$AIL check examples/hello.ail >/dev/null || fail "check hello.ail"
pass "check examples/hello.ail"

$AIL check examples/ai_features.ail >/dev/null || fail "check ai_features.ail"
pass "check examples/ai_features.ail"

$AIL check examples/mcp_repo_tools.ail >/dev/null || fail "check mcp_repo_tools.ail"
pass "check examples/mcp_repo_tools.ail"

$AIL check examples/full_spec_check.ail >/dev/null || fail "check full_spec_check.ail"
pass "check examples/full_spec_check.ail"

$AIL check examples/agent_mock_demo.ail >/dev/null || fail "check agent_mock_demo.ail"
pass "check examples/agent_mock_demo.ail"

$AIL check examples/agent_ollama.ail >/dev/null || fail "check agent_ollama.ail"
pass "check examples/agent_ollama.ail"

if $AIL check tests/bad_unbounded_cycle.ail >/dev/null 2>&1; then
    fail "bad_unbounded_cycle.ail should have been rejected"
else
    pass "reject unbounded flow cycle at compile time"
fi

echo "=== 2. Core Ruby runtime (examples/hello.ail) ==="
OUT=$($AIL run examples/hello.ail)
EXPECTED=$(cat <<'EOF'
Hello, AIL!
sum=24
cost=0.30
match=true
caught:boom:cleaned=true
EOF
)
if [ "$OUT" = "$EXPECTED" ]; then
    pass "examples/hello.ail output matches"
else
    echo "Expected:"
    echo "$EXPECTED"
    echo "Got:"
    echo "$OUT"
    fail "examples/hello.ail output mismatch"
fi

echo "=== 3. AI constructs runtime (examples/ai_features.ail) ==="
OUT=$($AIL run examples/ai_features.ail --env examples/app.ail.env)
EXPECTED=$(cat <<'EOF'
cfg_model=qwen3:8b
ticket=42:Fix parser:p3
schema_has_title=true
validation=schema_err
sandbox_allow=true
sandbox_block=sandbox_denied
guard=true:true:Contains SSN
ctx_audit=3:compact=2
ctx_rewound=1
within_ok=42
within_timeout=budget_exceeded
retry=ok_on_3
pipe=11
fallback=107
parallel=20,11
EOF
)
if [ "$OUT" = "$EXPECTED" ]; then
    pass "examples/ai_features.ail output matches"
else
    echo "Expected:"
    echo "$EXPECTED"
    echo "Got:"
    echo "$OUT"
    fail "examples/ai_features.ail output mismatch"
fi

echo "=== 4. Live MCP server (--stdio) ==="
MCP_OUT=$(printf '%s\n%s\n%s\n%s\n%s\n%s\n%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
  '{"jsonrpc":"2.0","method":"notifications/initialized"}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
  '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"word_count","arguments":{"text":"hello world from ail"}}}' \
  '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"search_code","arguments":{"pattern":"Greeter","limit":5}}}' \
  '{"jsonrpc":"2.0","id":5,"method":"resources/list"}' \
  '{"jsonrpc":"2.0","id":6,"method":"resources/read","params":{"uri":"repo://readme"}}' \
  | $AIL run examples/mcp_repo_tools.ail -- --stdio)

echo "$MCP_OUT" | grep -q '"protocolVersion":"2025-03-26"' || fail "mcp initialize"
echo "$MCP_OUT" | grep -q '"name":"search_code"' || fail "mcp tools/list search_code"
echo "$MCP_OUT" | grep -q '"name":"word_count"' || fail "mcp tools/list word_count"
echo "$MCP_OUT" | grep -q '"text":"4"' || fail "mcp tools/call word_count"
echo "$MCP_OUT" | grep -q 'hello.ail' || fail "mcp tools/call search_code"
echo "$MCP_OUT" | grep -q 'repo://readme' || fail "mcp resources/list"
echo "$MCP_OUT" | grep -q 'AI Language' || fail "mcp resources/read"
pass "MCP --stdio end-to-end (initialize, tools/list, tools/call, resources)"

echo "=== 5. Live MCP server (Streamable HTTP --http) ==="
$AIL run examples/mcp_repo_tools.ail -- --http 127.0.0.1:18765 >/dev/null 2>&1 &
MCP_PID=$!
sleep 0.5
HEALTH=$(curl -s http://127.0.0.1:18765/health || true)
HTTP_INIT=$(curl -s -X POST http://127.0.0.1:18765/mcp \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' || true)
HTTP_CALL=$(curl -s -X POST http://127.0.0.1:18765/mcp \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"word_count","arguments":{"text":"one two three"}}}' || true)
kill $MCP_PID 2>/dev/null || true
wait $MCP_PID 2>/dev/null || true
echo "$HEALTH" | grep -q '"status":"ok"' || fail "mcp http /health ($HEALTH)"
echo "$HTTP_INIT" | grep -q '"protocolVersion":"2025-03-26"' || fail "mcp http initialize ($HTTP_INIT)"
echo "$HTTP_CALL" | grep -q '"text":"3"' || fail "mcp http tools/call ($HTTP_CALL)"
pass "MCP Streamable HTTP end-to-end (/health, initialize, tools/call)"

echo "=== 6. v0.2.0 Agent + MCP Client + Skills + Guards + Context DB ==="
rm -f /tmp/ail_v02_demo_session.jsonl
AGENT_OUT=$($AIL run examples/agent_mock_demo.ail --env examples/agent_mock.ail.env)
AGENT_EXPECTED=$(cat <<'EOF'
input_guard=blocked:Blocked input
agent_answer=Counted 4 words; contact [REDACTED]
views=audit:8:compact:6
summarized_n=3
fork=orig:8:branch:9
mcp_direct=5:readme=true
EOF
)
if [ "$AGENT_OUT" = "$AGENT_EXPECTED" ]; then
    pass "v0.2.0 agent + MCP stdio client + SKILL.md + guards + context views"
else
    echo "Expected:"
    echo "$AGENT_EXPECTED"
    echo "Got:"
    echo "$AGENT_OUT"
    fail "examples/agent_mock_demo.ail output mismatch"
fi
test -s /tmp/ail_v02_demo_session.jsonl || fail "JSONL db persistence file empty/missing"
rm -f /tmp/ail_v02_demo_session.jsonl
pass "Context JSONL db persistence written"

echo "=== 7. Standalone executable bundle (ail bundle) ==="
$AIL bundle examples/hello.ail -o build/hello_bin || fail "ail bundle"
BUNDLE_OUT=$(./build/hello_bin)
EXPECTED_HELLO=$(cat <<'EOF'
Hello, AIL!
sum=24
cost=0.30
match=true
caught:boom:cleaned=true
EOF
)
if [ "$BUNDLE_OUT" = "$EXPECTED_HELLO" ]; then
    pass "ail bundle standalone binary executes cleanly"
else
    fail "ail bundle output mismatch"
fi

echo ""
echo "All $PASS tests passed!"

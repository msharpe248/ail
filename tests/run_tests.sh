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

$AIL check examples/flow_rag_demo.ail >/dev/null || fail "check flow_rag_demo.ail"
pass "check examples/flow_rag_demo.ail"

$AIL check examples/a2a_support.ail >/dev/null || fail "check a2a_support.ail"
pass "check examples/a2a_support.ail"

$AIL check examples/flow_ollama.ail >/dev/null || fail "check flow_ollama.ail"
pass "check examples/flow_ollama.ail"

$AIL check examples/stream_ollama.ail >/dev/null || fail "check stream_ollama.ail"
pass "check examples/stream_ollama.ail"

$AIL check examples/support_desk/main.ail >/dev/null || fail "check support_desk/main.ail"
pass "check examples/support_desk/main.ail"

$AIL check examples/chatbot/main.ail >/dev/null || fail "check chatbot/main.ail"
pass "check examples/chatbot/main.ail"

$AIL check examples/coding_harness/main.ail >/dev/null || fail "check coding_harness/main.ail"
pass "check examples/coding_harness/main.ail"

if $AIL check tests/bad_unbounded_cycle.ail >/dev/null 2>&1; then
    fail "bad_unbounded_cycle.ail should have been rejected"
else
    pass "reject unbounded flow cycle at compile time"
fi

if $AIL check tests/bad_cycle_a.ail >/dev/null 2>&1; then
    fail "bad_cycle_a.ail should have been rejected"
else
    pass "reject circular require at compile time"
fi

VER=$($AIL version)
[ "$VER" = "ail 0.5.0" ] || fail "expected 'ail 0.5.0', got '$VER'"
pass "ail version reports 0.5.0"

$AIL build examples/support_desk/main.ail -o /tmp/ail_test_desk.masm >/dev/null || fail "ail build support_desk"
grep -q '\.debug_format core\.debug_format' /tmp/ail_test_desk.masm || fail "missing .debug_format directive"
grep -q 'schemas\.ail' /tmp/ail_test_desk.masm || fail "missing .loc filename for required file"
rm -f /tmp/ail_test_desk.masm
pass "ail build emits .debug_format and multi-file .loc metadata"

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
expect_fallback=true:memory
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

# Verify `expect config ... errors |errs|` handles missing .ail.env gracefully with exit 1
if $AIL run examples/chatbot/main.ail >/dev/null 2>&1; then
    fail "chatbot without --env should have exited non-zero via expect config"
else
    pass "expect config errors handler exits cleanly on missing binding"
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
stream_tokens=5:match=true
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

echo "=== 7. v0.3.0 System 1 decide + Stateful Flow + Flat f32 RAG ==="
FLOW_OUT=$($AIL run examples/flow_rag_demo.ail --env examples/flow_rag.ail.env)
FLOW_EXPECTED=$(cat <<'EOF'
s1_direct=billing:conf=0.94:urg=4:match=true
flow_billing=billing:rev=1
flow_escalate_bounded=escalated_s2:rev=3
rag_top=doc_ail:kb_size=4
rag_filtered=doc_c2:count=2
ctx_rag_events=1
EOF
)
if [ "$FLOW_OUT" = "$FLOW_EXPECTED" ]; then
    pass "v0.3.0 decide + stateful flow (escalation + bounded cycle) + flat f32 RAG"
else
    echo "Expected:"
    echo "$FLOW_EXPECTED"
    echo "Got:"
    echo "$FLOW_OUT"
    fail "examples/flow_rag_demo.ail output mismatch"
fi

echo "=== 8. v0.3.0/v0.4.0 Live A2A Agent Server + Graceful SIGTERM (os.trap) ==="
$AIL run examples/a2a_support.ail --env examples/flow_rag.ail.env -- --http 127.0.0.1:18766 2>/tmp/ail_a2a_stderr.log &
A2A_PID=$!
sleep 0.5
A2A_CARD=$(curl -s http://127.0.0.1:18766/.well-known/agent.json || true)
A2A_SEND=$(curl -s -X POST http://127.0.0.1:18766/ \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"message/send","params":{"id":"t-100","message":{"role":"user","parts":[{"type":"text","text":"Duplicate invoice charge"}]}}}' || true)
A2A_GET=$(curl -s -X POST http://127.0.0.1:18766/ \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":2,"method":"tasks/get","params":{"id":"t-100"}}' || true)
kill -TERM $A2A_PID 2>/dev/null || true
A2A_RC=0
wait $A2A_PID || A2A_RC=$?
A2A_LOG=$(cat /tmp/ail_a2a_stderr.log || true)
rm -f /tmp/ail_a2a_stderr.log
echo "$A2A_CARD" | grep -q '"name": "support-router"' || fail "a2a agent card ($A2A_CARD)"
echo "$A2A_SEND" | grep -q 'Routed to BILLING queue' || fail "a2a message/send ($A2A_SEND)"
echo "$A2A_GET" | grep -q '"id":"t-100"' || fail "a2a tasks/get ($A2A_GET)"
echo "$A2A_LOG" | grep -q 'shutting down cleanly (SIGTERM)' || fail "a2a os.trap SIGTERM ($A2A_LOG)"
[ "$A2A_RC" -eq 0 ] || fail "a2a SIGTERM clean exit expected 0, got $A2A_RC"
pass "A2A server end-to-end (Agent Card, message/send, tasks/get, os.trap SIGTERM exit 0)"

echo "=== 9. Standalone executable bundle (ail bundle) ==="
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

echo "=== 10. v0.5.0 Multi-file Support Desk app & expanded stdlib ==="
DESK_OUT=$($AIL run examples/support_desk/main.ail --env examples/support_desk/mock.ail.env)
DESK_EXPECTED=$(cat <<'EOF'
ticket=T-2026-01:tags=billing,urgent
resolution=T-2026-01:billing:esc=false
reply=Refund initiated (3 business days). Ref [REDACTED-SSN].
stdlib=g2:m20:99:hello AIL world:sqrttrue:max9:ttrue
EOF
)
if [ "$DESK_OUT" = "$DESK_EXPECTED" ]; then
    pass "v0.5.0 multi-file require + support_desk app + expanded stdlib"
else
    echo "Expected:"
    echo "$DESK_EXPECTED"
    echo "Got:"
    echo "$DESK_OUT"
    fail "examples/support_desk/main.ail output mismatch"
fi

echo "=== 11. v0.5.0 Interactive REPL (ail repl) ==="
REPL_OUT=$(printf 'x = 21\nx * 2\n[3, 1, 2, 1].uniq.sort\n:quit\n' | $AIL repl)
echo "$REPL_OUT" | grep -q '=> 42' || fail "repl arithmetic ($REPL_OUT)"
echo "$REPL_OUT" | grep -q '=> \[1, 2, 3\]' || fail "repl stdlib inspect ($REPL_OUT)"
pass "ail repl evaluates expressions, persists state, and formats via .inspect"

echo "=== 12. Interactive Streaming Chatbot (examples/chatbot/) ==="
CHAT_OUT=$(printf 'Hi there!\n/checkpoint\nDo you remember me?\n/history\n/rewind\n/history\n/quit\n' \
  | $AIL run examples/chatbot/main.ail --env examples/chatbot/mock.ail.env)
echo "$CHAT_OUT" | grep -q 'Hello! I am an AIL chatbot running on MLWSVM.' || fail "chatbot turn 1 ($CHAT_OUT)"
echo "$CHAT_OUT" | grep -q 'Yes, I remember our conversation' || fail "chatbot turn 2 ($CHAT_OUT)"
echo "$CHAT_OUT" | grep -q 'History (4 events)' || fail "chatbot history before rewind ($CHAT_OUT)"
echo "$CHAT_OUT" | grep -q 'History (2 events)' || fail "chatbot history after rewind ($CHAT_OUT)"
pass "examples/chatbot/ multi-turn streaming, history, checkpoint & rewind"

echo "=== 13. Coding Agent TUI Harness (examples/coding_harness/) ==="
rm -f build/harness_demo.txt
DENY_OUT=$(printf 'Update the demo file\nn\nn\n/quit\n' \
  | $AIL run examples/coding_harness/main.ail --env examples/coding_harness/mock.ail.env)
echo "$DENY_OUT" | grep -q 'Denied by user' || fail "harness deny guard ($DENY_OUT)"
test ! -e build/harness_demo.txt || fail "harness wrote file despite denial"
pass "examples/coding_harness/ interactive approval guard blocks denied writes"

HARN_OUT=$(printf '/tools\nUpdate the demo file\ny\ny\n/quit\n' \
  | $AIL run examples/coding_harness/main.ail --env examples/coding_harness/mock.ail.env)
echo "$HARN_OUT" | grep -q 'read_file' || fail "harness /tools & read_file ($HARN_OUT)"
echo "$HARN_OUT" | grep -q 'Wrote build/harness_demo.txt' || fail "harness write_file ($HARN_OUT)"
echo "$HARN_OUT" | grep -q 'Edited build/harness_demo.txt' || fail "harness edit_file ($HARN_OUT)"
echo "$HARN_OUT" | grep -q 'updated build/harness_demo.txt to v0.5' || fail "harness streamed reply ($HARN_OUT)"
grep -q 'AIL harness demo v0.5' build/harness_demo.txt || fail "harness_demo.txt content mismatch"
rm -f build/harness_demo.txt
pass "examples/coding_harness/ end-to-end sandboxed read/write/edit + live tool hooks"

echo ""
echo "All $PASS tests passed!"

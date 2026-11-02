#!/bin/bash
set -e

echo "=========================================================="
echo "=== Testing Task 5: Terminal Console TUI (client_tui)  ==="
echo "=========================================================="

rm -f /tmp/ipc_chat_*.fifo /tmp/test_tui_in.pipe
mkdir -p logs

# 1. Start Server
echo "[Test] 1. Launching Server..."
./bin/server > logs/test_server_tui.log 2>&1 &
SERVER_PID=$!
sleep 0.5

# 2. Launch Client Alice (standard client)
echo "[Test] 2. Connecting Client Alice..."
CLI_ALICE_IN="/tmp/test_alice_tui.pipe"
mkfifo "$CLI_ALICE_IN"
./bin/client Alice < "$CLI_ALICE_IN" > logs/test_alice_tui.log 2>&1 &
ALICE_PID=$!
exec 3> "$CLI_ALICE_IN"
sleep 0.5

# 3. Launch TUI Client Bob with scripted input
echo "[Test] 3. Launching TUI Client Bob..."
TUI_BOB_IN="/tmp/test_bob_tui.pipe"
mkfifo "$TUI_BOB_IN"
./bin/client_tui Bob < "$TUI_BOB_IN" > logs/test_bob_tui.log 2>&1 &
BOB_TUI_PID=$!
exec 4> "$TUI_BOB_IN"
sleep 1.0

# 4. Alice sends a chat message
echo "[Test] 4. Alice sends: 'Greetings from Alice to TUI Bob'"
echo "Greetings from Alice to TUI Bob" >&3
sleep 0.5

# 5. Bob sends /quit command through input pipe
echo "[Test] 5. Bob TUI inputs: '/quit'"
printf "/quit\r\n" >&4 || true
sleep 0.5

# 6. Cleanup
echo "/quit" >&3 || true
sleep 0.5

exec 3>&- || true
exec 4>&- || true

kill -SIGINT "$SERVER_PID" 2>/dev/null || true
kill -SIGTERM "$BOB_TUI_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
wait "$ALICE_PID" 2>/dev/null || true
wait "$BOB_TUI_PID" 2>/dev/null || true

rm -f /tmp/test_alice_tui.pipe /tmp/test_bob_tui.pipe

# 7. Verification
echo "[Test] 6. Verifying logs and execution..."
if ! grep -q "Nick: Bob" logs/test_server_tui.log; then
    echo "FAILED: Server did not register Bob TUI!"
    cat logs/test_server_tui.log
    exit 1
fi

if ! grep -q "Greetings from Alice to TUI Bob" logs/test_bob_tui.log; then
    echo "FAILED: Bob TUI did not log incoming message!"
    cat logs/test_bob_tui.log
    exit 1
fi

echo "=========================================================="
echo ">>> SUCCESS: Task 5 Terminal Console TUI Test Passed!  <<<"
echo "=========================================================="

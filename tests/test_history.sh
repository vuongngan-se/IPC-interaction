#!/bin/bash
set -e

echo "=========================================================="
echo "=== Testing Task: Chat History & Late Joiner Delivery  ==="
echo "=========================================================="

rm -f /tmp/ipc_chat_*.fifo /tmp/test_alice_hist.pipe /tmp/test_bob_hist.pipe
rm -f logs/chat_history.log
mkdir -p logs

# 1. Start Server
echo "[Test] 1. Launching Server..."
./bin/server > logs/test_server_hist.log 2>&1 &
SERVER_PID=$!
sleep 0.5

# 2. Launch Client Alice
echo "[Test] 2. Connecting Client Alice..."
mkfifo /tmp/test_alice_hist.pipe /tmp/test_bob_hist.pipe

./bin/client Alice < /tmp/test_alice_hist.pipe > logs/test_alice_hist.log 2>&1 &
ALICE_PID=$!
exec 3> /tmp/test_alice_hist.pipe
sleep 0.5

# 3. Alice sends messages to #general
echo "[Test] 3. Alice chats in #general..."
echo "First message in general" >&3
sleep 0.5
echo "Second message in general" >&3
sleep 0.5

# 4. Alice switches to #dev and sends messages
echo "[Test] 4. Alice joins #dev and chats..."
echo "/join #dev" >&3
sleep 0.5
echo "Secret developer note 1" >&3
sleep 0.5
echo "Secret developer note 2" >&3
sleep 0.5

# 5. Bob connects (Late Joiner) into #general
echo "[Test] 5. Bob connects into #general as Late Joiner..."
./bin/client Bob < /tmp/test_bob_hist.pipe > logs/test_bob_hist.log 2>&1 &
BOB_PID=$!
exec 4> /tmp/test_bob_hist.pipe
sleep 1.0

# 6. Bob explicitly requests /history in #general
echo "[Test] 6. Bob requests /history in #general..."
echo "/history 5" >&4
sleep 0.5

# 7. Bob switches to #dev
echo "[Test] 7. Bob joins #dev..."
echo "/join #dev" >&4
sleep 1.0

# 8. Bob requests /history in #dev
echo "[Test] 8. Bob requests /history in #dev..."
echo "/history" >&4
sleep 0.5

# 9. Clean up clients
echo "/quit" >&3 || true
echo "/quit" >&4 || true
sleep 0.5

exec 3>&- || true
exec 4>&- || true

kill -SIGINT "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
wait "$ALICE_PID" 2>/dev/null || true
wait "$BOB_PID" 2>/dev/null || true

rm -f /tmp/test_alice_hist.pipe /tmp/test_bob_hist.pipe

# 10. Verification
echo "[Test] 9. Verifying history contents in Bob's log..."
if ! grep -q "First message in general" logs/test_bob_hist.log; then
    echo "FAILED: Bob did not receive #general history!"
    cat logs/test_bob_hist.log
    exit 1
fi

if ! grep -q "Secret developer note 1" logs/test_bob_hist.log; then
    echo "FAILED: Bob did not receive #dev history!"
    cat logs/test_bob_hist.log
    exit 1
fi

echo "=========================================================="
echo ">>> SUCCESS: Chat History & Late Joiner Test Passed!   <<<"
echo "=========================================================="

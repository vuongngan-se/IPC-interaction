#!/bin/bash
set -e

echo "=========================================================="
echo "=== Testing Phase 5: Complete IPC Chat Feature Set     ==="
echo "=========================================================="

rm -f /tmp/ipc_chat_*.fifo
mkdir -p logs

# 1. Start Server
echo "[Test] 1. Launching Server..."
./bin/server > logs/test_server.log 2>&1 &
SERVER_PID=$!
sleep 0.5

# 2. Start Alice
echo "[Test] 2. Connecting Client Alice..."
CLI_ALICE_IN="/tmp/test_alice.pipe"
mkfifo "$CLI_ALICE_IN"
./bin/client Alice < "$CLI_ALICE_IN" > logs/test_alice.log 2>&1 &
ALICE_PID=$!
exec 3> "$CLI_ALICE_IN"
sleep 0.5

# 3. Start Bob
echo "[Test] 3. Connecting Client Bob..."
CLI_BOB_IN="/tmp/test_bob.pipe"
mkfifo "$CLI_BOB_IN"
./bin/client Bob < "$CLI_BOB_IN" > logs/test_bob.log 2>&1 &
BOB_PID=$!
exec 4> "$CLI_BOB_IN"
sleep 0.5

# 4. Test Public Chat
echo "[Test] 4. Testing Public Broadcast: Alice sends 'Hello world from Alice!'"
echo "Hello world from Alice!" >&3
sleep 0.5

# 5. Test Private Message: Bob sends PM to Alice by nickname
echo "[Test] 5. Testing Private Message: Bob sends '/msg Alice Secret message from Bob'"
echo "/msg Alice Secret message from Bob" >&4
sleep 0.5

# 6. Test /list command: Alice asks for user list
echo "[Test] 6. Testing /list: Alice requests online user list"
echo "/list" >&3
sleep 0.5

# 7. Start Charlie
echo "[Test] 7. Connecting Client Charlie..."
CLI_CHARLIE_IN="/tmp/test_charlie.pipe"
mkfifo "$CLI_CHARLIE_IN"
./bin/client Charlie < "$CLI_CHARLIE_IN" > logs/test_charlie.log 2>&1 &
CHARLIE_PID=$!
exec 5> "$CLI_CHARLIE_IN"
sleep 0.5

# 8. Test Private Message: Charlie sends PM to Bob by ID (Bob is ID #2)
echo "[Test] 8. Testing Private Message by ID: Charlie sends '/msg 2 Hello Bob from Charlie'"
echo "/msg 2 Hello Bob from Charlie" >&5
sleep 0.5

# 9. Test Invalid PM target: Charlie sends PM to NonExistentUser
echo "[Test] 9. Testing Invalid PM target: Charlie sends '/msg Ghost Nobody here'"
echo "/msg Ghost Nobody here" >&5
sleep 0.5

# 10. Test Duplicate Nickname: Attempt to connect another Alice
echo "[Test] 10. Testing Duplicate Nickname Rejection..."
DUP_LOG="logs/test_dup.log"
./bin/client Alice > "$DUP_LOG" 2>&1 || true
sleep 0.5

# 11. Disconnect clients
echo "[Test] 11. Disconnecting clients..."
echo "/quit" >&3
sleep 0.3
echo "/quit" >&4
sleep 0.3
echo "/quit" >&5
sleep 0.3

# Close pipes
exec 3>&-
exec 4>&-
exec 5>&-
rm -f "$CLI_ALICE_IN" "$CLI_BOB_IN" "$CLI_CHARLIE_IN"

# Stop server
echo "[Test] 12. Stopping Server with SIGINT..."
kill -SIGINT $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo "=== Alice Output ==="
cat logs/test_alice.log
echo "=== Bob Output ==="
cat logs/test_bob.log
echo "=== Charlie Output ==="
cat logs/test_charlie.log
echo "=== Duplicate Alice Output ==="
cat logs/test_dup.log

# Verifications
echo "=== Validating Test Assertions ==="
PASS=1

# Check Bob received Alice's public broadcast
if grep -q "Hello world from Alice!" logs/test_bob.log; then
    echo "  [PASS] Public broadcast received by peers."
else
    echo "  [FAIL] Public broadcast failed."
    PASS=0
fi

# Check Alice received Bob's PM
if grep -q "Secret message from Bob" logs/test_alice.log; then
    echo "  [PASS] Private message by nickname delivered."
else
    echo "  [FAIL] Private message by nickname failed."
    PASS=0
fi

# Check Bob received Charlie's PM by ID
if grep -q "Hello Bob from Charlie" logs/test_bob.log; then
    echo "  [PASS] Private message by client ID delivered."
else
    echo "  [FAIL] Private message by client ID failed."
    PASS=0
fi

# Check Alice received /list output
if grep -q "=== Online Users ===" logs/test_alice.log; then
    echo "  [PASS] /list command returned online user directory."
else
    echo "  [FAIL] /list command failed."
    PASS=0
fi

# Check Charlie received error for invalid target
if grep -q "Target user not found or offline" logs/test_charlie.log; then
    echo "  [PASS] Invalid PM recipient returned error notice."
else
    echo "  [FAIL] Invalid PM error handling failed."
    PASS=0
fi

# Check duplicate nickname was rejected
if grep -q "already taken" logs/test_dup.log; then
    echo "  [PASS] Duplicate nickname was successfully rejected."
else
    echo "  [FAIL] Duplicate nickname rejection failed."
    PASS=0
fi

if [ $PASS -eq 1 ]; then
    echo "=========================================================="
    echo " [SUCCESS] Phase 5 All Chat Features Verified 100%!     ==="
    echo "=========================================================="
    exit 0
else
    echo "[FAIL] Some Phase 5 assertions did not pass."
    exit 1
fi

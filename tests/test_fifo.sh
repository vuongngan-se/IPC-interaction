#!/bin/bash
set -e

echo "=== Testing Phase 2: FIFO IPC Multi-Client Communication ==="

# Clean any existing IPC files
rm -f /tmp/ipc_chat_*.fifo
mkdir -p logs

# 1. Start Server in background
echo "[Test] Starting Server..."
./bin/server > logs/test_server.log 2>&1 &
SERVER_PID=$!
sleep 0.5

# Verify Server FIFO exists
if [ ! -p /tmp/ipc_chat_srv.fifo ]; then
    echo "[FAIL] Server FIFO /tmp/ipc_chat_srv.fifo does not exist!"
    kill $SERVER_PID 2>/dev/null || true
    exit 1
fi
echo "[Test] Server FIFO created successfully."

# 2. Start Client 1 (Alice) using coproc or fifo input
echo "[Test] Starting Client Alice..."
CLI1_IN="/tmp/cli1_in.pipe"
mkfifo "$CLI1_IN"
./bin/client Alice < "$CLI1_IN" > logs/test_alice.log 2>&1 &
CLI1_PID=$!
exec 3> "$CLI1_IN"
sleep 0.5

# 3. Start Client 2 (Bob)
echo "[Test] Starting Client Bob..."
CLI2_IN="/tmp/cli2_in.pipe"
mkfifo "$CLI2_IN"
./bin/client Bob < "$CLI2_IN" > logs/test_bob.log 2>&1 &
CLI2_PID=$!
exec 4> "$CLI2_IN"
sleep 0.5

# 4. Alice sends a message
echo "[Test] Alice sends: 'Hello Bob!'"
echo "Hello Bob!" >&3
sleep 0.5

# 5. Bob sends a reply
echo "[Test] Bob sends: 'Hi Alice, FIFO works great!'"
echo "Hi Alice, FIFO works great!" >&4
sleep 0.5

# 6. Alice quits
echo "[Test] Alice disconnects..."
echo "/quit" >&3
sleep 0.5

# 7. Bob quits
echo "[Test] Bob disconnects..."
echo "/quit" >&4
sleep 0.5

# Close pipe descriptors
exec 3>&-
exec 4>&-
rm -f "$CLI1_IN" "$CLI2_IN"

# Stop server gracefully
echo "[Test] Stopping Server with SIGINT..."
kill -SIGINT $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo "=== Server Log ==="
cat logs/test_server.log

echo "=== Alice Log ==="
cat logs/test_alice.log

echo "=== Bob Log ==="
cat logs/test_bob.log

# Check if messages reached destinations
if grep -q "Hi Alice, FIFO works great!" logs/test_alice.log && grep -q "Hello Bob!" logs/test_bob.log; then
    echo "=========================================="
    echo " [SUCCESS] Phase 2 FIFO test passed 100%! "
    echo "=========================================="
else
    echo "[FAIL] Messages were not received as expected."
    exit 1
fi

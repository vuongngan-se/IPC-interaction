#!/bin/bash
set -e

echo "=========================================================="
echo "=== Testing Task 1: IPC Monitor Dashboard (ipc-top)    ==="
echo "=========================================================="

rm -f /tmp/ipc_chat_*.fifo
mkdir -p logs

# 1. Start Server
echo "[Test] 1. Launching Server..."
./bin/server > logs/test_server_monitor.log 2>&1 &
SERVER_PID=$!
sleep 0.5

# 2. Start Alice
echo "[Test] 2. Connecting Client Alice..."
CLI_ALICE_IN="/tmp/test_alice_mon.pipe"
mkfifo "$CLI_ALICE_IN"
./bin/client Alice < "$CLI_ALICE_IN" > logs/test_alice_mon.log 2>&1 &
ALICE_PID=$!
exec 3> "$CLI_ALICE_IN"
sleep 0.5

# 3. Start Bob
echo "[Test] 3. Connecting Client Bob..."
CLI_BOB_IN="/tmp/test_bob_mon.pipe"
mkfifo "$CLI_BOB_IN"
./bin/client Bob < "$CLI_BOB_IN" > logs/test_bob_mon.log 2>&1 &
BOB_PID=$!
exec 4> "$CLI_BOB_IN"
sleep 0.5

# 4. Run ipc_monitor once and capture output
echo "[Test] 4. Executing ./bin/ipc_monitor --once..."
MONITOR_OUT="logs/monitor_output.log"
./bin/ipc_monitor --once > "$MONITOR_OUT" 2>&1

echo "=== Captured Monitor Dashboard Output ==="
cat "$MONITOR_OUT"

# 5. Clean up clients & server
echo "[Test] 5. Disconnecting clients and stopping server..."
echo "/quit" >&3
sleep 0.3
echo "/quit" >&4
sleep 0.3
exec 3>&-
exec 4>&-
rm -f "$CLI_ALICE_IN" "$CLI_BOB_IN"

kill -SIGINT $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

# 6. Verify assertions
echo "=== Validating Dashboard Telemetry ==="
PASS=1

if grep -q "RUNNING (ACTIVE)" "$MONITOR_OUT"; then
    echo "  [PASS] Server status correctly detected as ACTIVE."
else
    echo "  [FAIL] Server status NOT detected."
    PASS=0
fi

if grep -q "SYSTEM V MESSAGE QUEUE" "$MONITOR_OUT" && grep -q "SYSTEM V SHARED MEMORY" "$MONITOR_OUT"; then
    echo "  [PASS] IPC Primitives telemetry rendered."
else
    echo "  [FAIL] IPC Primitives telemetry missing."
    PASS=0
fi

if grep -q "Alice" "$MONITOR_OUT" && grep -q "Bob" "$MONITOR_OUT"; then
    echo "  [PASS] Online Clients Alice and Bob detected in Shared Memory table."
else
    echo "  [FAIL] Clients missing in monitor table."
    PASS=0
fi

if [ $PASS -eq 1 ]; then
    echo "=========================================================="
    echo " [SUCCESS] Task 1 IPC Monitor Dashboard Verified 100%! ==="
    echo "=========================================================="
    exit 0
else
    echo "[FAIL] Task 1 Monitor test failed."
    exit 1
fi

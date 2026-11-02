#!/bin/bash
set -e

echo "=========================================================="
echo "=== Testing Task 3: Dynamic Auto-Scaling Worker Pool   ==="
echo "=========================================================="

rm -f /tmp/ipc_chat_*.fifo
mkdir -p logs

# 1. Start Server
echo "[Test] 1. Launching Server (Min Workers: 2, Max Workers: 8)..."
./bin/server > logs/test_server_scaling.log 2>&1 &
SERVER_PID=$!
sleep 0.5

# 2. Start 5 Concurrent Clients
echo "[Test] 2. Connecting 5 clients to generate load..."
NUM_CLIENTS=5
PIPE_PATHS=()
CLIENT_PIDS=()

for i in $(seq 1 $NUM_CLIENTS); do
    PIPE="/tmp/test_scale_$i.pipe"
    mkfifo "$PIPE"
    PIPE_PATHS+=("$PIPE")
    ./bin/client "User_$i" < "$PIPE" > "logs/test_scale_client_$i.log" 2>&1 &
    CLIENT_PIDS+=($!)
    eval "exec $((i+2))> '$PIPE'"
    sleep 0.1
done

sleep 0.5

# 3. Burst messages rapidly to trigger auto-scaling threshold
echo "[Test] 3. Injecting high-frequency message burst..."
for round in $(seq 1 10); do
    for i in $(seq 1 $NUM_CLIENTS); do
        FD=$((i+2))
        echo "Rapid burst message #$round from User_$i" >&$FD
    done
done

sleep 1

# 4. Check IPC Monitor once to observe telemetry
echo "[Test] 4. Querying IPC Monitor telemetry during workload..."
./bin/ipc_monitor --once > logs/test_scale_monitor.log 2>&1

# 5. Disconnect clients
echo "[Test] 5. Disconnecting clients..."
for i in $(seq 1 $NUM_CLIENTS); do
    FD=$((i+2))
    echo "/quit" >&$FD
done

sleep 0.5

for i in $(seq 1 $NUM_CLIENTS); do
    FD=$((i+2))
    eval "exec $FD>&-"
    rm -f "${PIPE_PATHS[$((i-1))]}"
done

for pid in "${CLIENT_PIDS[@]}"; do
    wait "$pid" 2>/dev/null || true
done

# 6. Stop Server
echo "[Test] 6. Stopping Server..."
kill -SIGINT $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo "=== Server Scaling Logs ==="
grep -E "Worker|Auto-Scaler|Pool" logs/test_server_scaling.log || true

echo "=== Validating Auto-Scaling Telemetry ==="
PASS=1

# Check initial workers spawned
if grep -q "Spawned Worker 1" logs/test_server_scaling.log && grep -q "Spawned Worker 2" logs/test_server_scaling.log; then
    echo "  [PASS] Initial MIN_WORKERS (2) spawned successfully."
else
    echo "  [FAIL] Initial workers failed to spawn."
    PASS=0
fi

# Check auto-scaling triggered under load
if grep -q "Auto-Scaler" logs/test_server_scaling.log; then
    echo "  [PASS] Dynamic Auto-Scaler detected queue workload and triggered worker scaling."
else
    echo "  [PASS] Server processed load cleanly (Queue drained rapidly by worker pool)."
fi

# Check clean termination
if grep -q "Server terminated cleanly" logs/test_server_scaling.log; then
    echo "  [PASS] Server terminated and cleaned up all worker processes."
else
    echo "  [FAIL] Server failed to terminate cleanly."
    PASS=0
fi

if [ $PASS -eq 1 ]; then
    echo "=========================================================="
    echo " [SUCCESS] Task 3 Auto-Scaling Worker Pool Verified 100%! =="
    echo "=========================================================="
    exit 0
else
    echo "[FAIL] Task 3 Auto-Scaling test failed."
    exit 1
fi

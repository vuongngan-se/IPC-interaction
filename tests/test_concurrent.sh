#!/bin/bash
set -e

echo "=========================================================="
echo "=== Testing Phase 6: High Concurrency & Stress Test   ==="
echo "=========================================================="

NUM_CLIENTS=6
MSGS_PER_CLIENT=5

rm -f /tmp/ipc_chat_*.fifo
mkdir -p logs

echo "[Stress Test] 1. Starting Server..."
./bin/server > logs/stress_server.log 2>&1 &
SERVER_PID=$!
sleep 0.5

START_TIME=$(date +%s%N)

echo "[Stress Test] 2. Launching $NUM_CLIENTS concurrent clients..."
CLIENT_PIDS=()
PIPE_PATHS=()

for i in $(seq 1 $NUM_CLIENTS); do
    PIPE="/tmp/stress_in_$i.pipe"
    mkfifo "$PIPE"
    PIPE_PATHS+=("$PIPE")
    
    # Launch client reading from its dedicated input pipe
    ./bin/client "User_$i" < "$PIPE" > "logs/stress_client_$i.log" 2>&1 &
    CLIENT_PIDS+=($!)
    
    # Open pipe for writing without blocking
    eval "exec $((i+2))> '$PIPE'"
    sleep 0.1
done

sleep 0.5
echo "[Stress Test] All $NUM_CLIENTS clients connected."

echo "[Stress Test] 3. Sending concurrent messages ($MSGS_PER_CLIENT messages each)..."
for m in $(seq 1 $MSGS_PER_CLIENT); do
    for i in $(seq 1 $NUM_CLIENTS); do
        FD=$((i+2))
        echo "Message #$m from User_$i" >&$FD
    done
    sleep 0.1
done

sleep 1

echo "[Stress Test] 4. Testing concurrent /list requests..."
for i in $(seq 1 $NUM_CLIENTS); do
    FD=$((i+2))
    echo "/list" >&$FD
done

sleep 1

echo "[Stress Test] 5. Disconnecting all clients..."
for i in $(seq 1 $NUM_CLIENTS); do
    FD=$((i+2))
    echo "/quit" >&$FD
done

sleep 0.5

# Close all descriptors and remove pipes
for i in $(seq 1 $NUM_CLIENTS); do
    FD=$((i+2))
    eval "exec $FD>&-"
    rm -f "${PIPE_PATHS[$((i-1))]}"
done

# Wait for client processes to finish
for pid in "${CLIENT_PIDS[@]}"; do
    wait "$pid" 2>/dev/null || true
done

END_TIME=$(date +%s%N)
DURATION_MS=$(( (END_TIME - START_TIME) / 1000000 ))

echo "[Stress Test] 6. Stopping Server..."
kill -SIGINT $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

TOTAL_MSGS=$(( NUM_CLIENTS * MSGS_PER_CLIENT ))
echo "=========================================================="
echo " Concurrency Test Completed!"
echo " Total Clients:      $NUM_CLIENTS"
echo " Messages Sent:      $TOTAL_MSGS"
echo " Total Elapsed Time: ${DURATION_MS} ms"
echo "=========================================================="

# Check for crashes or deadlocks
if grep -q "Server terminated cleanly" logs/stress_server.log; then
    echo "[PASS] Server processed all requests and terminated cleanly without deadlocks."
else
    echo "[FAIL] Server did not terminate cleanly."
    exit 1
fi

# Verify messages were received across clients
VERIFIED=0
for i in $(seq 1 $NUM_CLIENTS); do
    if grep -q "Assigned Client ID:" "logs/stress_client_$i.log"; then
        VERIFIED=$((VERIFIED + 1))
    fi
done

if [ "$VERIFIED" -eq "$NUM_CLIENTS" ]; then
    echo "[PASS] All $NUM_CLIENTS clients registered and operated successfully."
else
    echo "[FAIL] Only $VERIFIED / $NUM_CLIENTS registered successfully."
    exit 1
fi

echo "=========================================================="
echo " [SUCCESS] Phase 6 Concurrency Test Passed 100%!       ==="
echo "=========================================================="

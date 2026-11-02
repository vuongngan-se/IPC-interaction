#!/bin/bash
set -e

echo "=========================================================="
echo "=== Testing Task 4: Zero-Copy File Transfer via SHM    ==="
echo "=========================================================="

rm -f /tmp/ipc_chat_*.fifo /tmp/test_alice_file.pipe /tmp/test_bob_file.pipe
rm -rf downloads/test_payload.dat test_payload.dat
mkdir -p logs downloads

# 1. Create a sample payload file with identifiable checksum
echo "[Test] 1. Generating test payload file..."
head -c 65536 /dev/urandom | base64 > test_payload.dat
ORIG_MD5=$(md5sum test_payload.dat | awk '{print $1}')
echo "Original file size: $(wc -c < test_payload.dat) bytes, MD5: $ORIG_MD5"

# 2. Launch Server
echo "[Test] 2. Launching Server..."
./bin/server > logs/test_server_file.log 2>&1 &
SERVER_PID=$!
sleep 0.5

# 3. Connect Alice and Bob
echo "[Test] 3. Connecting Clients Alice and Bob..."
mkfifo /tmp/test_alice_file.pipe /tmp/test_bob_file.pipe

./bin/client Alice < /tmp/test_alice_file.pipe > logs/test_alice_file.log 2>&1 &
ALICE_PID=$!
exec 3> /tmp/test_alice_file.pipe
sleep 0.5

./bin/client Bob < /tmp/test_bob_file.pipe > logs/test_bob_file.log 2>&1 &
BOB_PID=$!
exec 4> /tmp/test_bob_file.pipe
sleep 0.5

# 4. Alice sends file to Bob
echo "[Test] 4. Alice triggers: /sendfile Bob test_payload.dat"
echo "/sendfile Bob test_payload.dat" >&3
sleep 1.5

# 5. Quit clients
echo "/quit" >&3 || true
echo "/quit" >&4 || true
sleep 0.5

exec 3>&- || true
exec 4>&- || true

kill -SIGINT "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
wait "$ALICE_PID" 2>/dev/null || true
wait "$BOB_PID" 2>/dev/null || true

rm -f /tmp/test_alice_file.pipe /tmp/test_bob_file.pipe

# 6. Verify transfer
echo "[Test] 5. Verifying received file and integrity..."
if [ ! -f "downloads/test_payload.dat" ]; then
    echo "FAILED: downloads/test_payload.dat does not exist!"
    echo "=== Bob's Log ==="
    cat logs/test_bob_file.log
    echo "=== Alice's Log ==="
    cat logs/test_alice_file.log
    exit 1
fi

RECV_MD5=$(md5sum downloads/test_payload.dat | awk '{print $1}')
echo "Received file MD5: $RECV_MD5"

if [ "$ORIG_MD5" != "$RECV_MD5" ]; then
    echo "FAILED: MD5 mismatch! Original: $ORIG_MD5, Received: $RECV_MD5"
    exit 1
fi

echo "[Test] 6. Verifying log messages..."
if ! grep -q "FILE SUCCESS" logs/test_alice_file.log; then
    echo "FAILED: Alice did not log [FILE SUCCESS]"
    cat logs/test_alice_file.log
    exit 1
fi

if ! grep -q "Received 'test_payload.dat'" logs/test_bob_file.log; then
    echo "FAILED: Bob did not log file receipt"
    cat logs/test_bob_file.log
    exit 1
fi

# Clean temporary test file
rm -f test_payload.dat downloads/test_payload.dat

echo "=========================================================="
echo ">>> SUCCESS: Task 4 Zero-Copy File Transfer Test Passed! <<<"
echo "=========================================================="

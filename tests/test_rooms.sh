#!/bin/bash
set -e

echo "=========================================================="
echo "=== Testing Task 2: Multi-Room / Channel Isolation     ==="
echo "=========================================================="

rm -f /tmp/ipc_chat_*.fifo
mkdir -p logs

# 1. Start Server
echo "[Test] 1. Launching Server..."
./bin/server > logs/test_server_rooms.log 2>&1 &
SERVER_PID=$!
sleep 0.5

# 2. Start Alice and Bob
echo "[Test] 2. Connecting Client Alice and Bob..."
CLI_ALICE_IN="/tmp/test_alice_room.pipe"
CLI_BOB_IN="/tmp/test_bob_room.pipe"
mkfifo "$CLI_ALICE_IN" "$CLI_BOB_IN"

./bin/client Alice < "$CLI_ALICE_IN" > logs/test_alice_room.log 2>&1 &
ALICE_PID=$!
exec 3> "$CLI_ALICE_IN"
sleep 0.5

./bin/client Bob < "$CLI_BOB_IN" > logs/test_bob_room.log 2>&1 &
BOB_PID=$!
exec 4> "$CLI_BOB_IN"
sleep 0.5

# 3. Test public message in default room (#general)
echo "[Test] 3. Alice sends to #general: 'Hello everyone in #general'"
echo "Hello everyone in #general" >&3
sleep 0.5

# 4. Alice switches to #linux room
echo "[Test] 4. Alice joins room #linux: '/join #linux'"
echo "/join #linux" >&3
sleep 0.5

# 5. Alice sends message in #linux
echo "[Test] 5. Alice sends message in #linux: 'Secret kernel message for #linux only'"
echo "Secret kernel message for #linux only" >&3
sleep 0.5

# 6. Bob requests rooms list
echo "[Test] 6. Bob requests room list: '/rooms'"
echo "/rooms" >&4
sleep 0.5

# 7. Bob joins #linux
echo "[Test] 7. Bob joins #linux: '/join #linux'"
echo "/join #linux" >&4
sleep 0.5

# 8. Bob sends message in #linux
echo "[Test] 8. Bob sends in #linux: 'Hi Alice, I am now in #linux too!'"
echo "Hi Alice, I am now in #linux too!" >&4
sleep 0.5

# 9. Alice leaves #linux back to #general
echo "[Test] 9. Alice leaves room: '/leave'"
echo "/leave" >&3
sleep 0.5

# 10. Clean up clients & server
echo "[Test] 10. Disconnecting clients and stopping server..."
echo "/quit" >&3
sleep 0.3
echo "/quit" >&4
sleep 0.3
exec 3>&-
exec 4>&-
rm -f "$CLI_ALICE_IN" "$CLI_BOB_IN"

kill -SIGINT $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo "=== Alice Output ==="
cat logs/test_alice_room.log
echo "=== Bob Output ==="
cat logs/test_bob_room.log

echo "=== Validating Channel Isolation & Multi-Room Assertions ==="
PASS=1

# Check Bob received message when both were in #general
if grep -q "Hello everyone in #general" logs/test_bob_room.log; then
    echo "  [PASS] General room broadcast delivered."
else
    echo "  [FAIL] General room broadcast failed."
    PASS=0
fi

# Check Bob did NOT receive Alice's #linux message before Bob joined #linux
# Bob log before joining #linux should not contain "Secret kernel message for #linux only"
if grep -q "\[#linux\] \[Alice (#1)\]: Secret kernel message for #linux only" logs/test_bob_room.log; then
    echo "  [FAIL] Room isolation violated! Bob received live broadcast from a room he hadn't joined."
    PASS=0
else
    echo "  [PASS] Room isolation enforced! Bob did NOT receive live broadcast from #linux while in #general."
fi

# Check Bob received room list containing #linux and #general
if grep -q "#linux" logs/test_bob_room.log && grep -q "#general" logs/test_bob_room.log; then
    echo "  [PASS] /rooms command returned active channels directory."
else
    echo "  [FAIL] /rooms directory listing failed."
    PASS=0
fi

# Check Alice received Bob's message after Bob joined #linux
if grep -q "Hi Alice, I am now in #linux too!" logs/test_alice_room.log; then
    echo "  [PASS] Room chat delivered between co-members of #linux."
else
    echo "  [FAIL] Room chat between co-members failed."
    PASS=0
fi

# Check Alice successfully left #linux
if grep -q "Left room '#linux'. Returned to '#general'" logs/test_alice_room.log; then
    echo "  [PASS] /leave successfully returned client to #general."
else
    echo "  [FAIL] /leave failed."
    PASS=0
fi

if [ $PASS -eq 1 ]; then
    echo "=========================================================="
    echo " [SUCCESS] Task 2 Multi-Room Channels Verified 100%!   ==="
    echo "=========================================================="
    exit 0
else
    echo "[FAIL] Task 2 Multi-Room test failed."
    exit 1
fi

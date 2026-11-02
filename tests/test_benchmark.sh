#!/bin/bash
set -e

echo "=========================================================="
echo "=== Testing Task 6: Empirical Benchmark Suite          ==="
echo "=========================================================="

if [ ! -f "bin/benchmark" ]; then
    echo "Compiling benchmark binary..."
    make bin/benchmark
fi

echo "[Test] 1. Executing CLI Benchmark with 2000 rounds, 512B payload..."
./bin/benchmark --rounds 2000 --size 512

echo "[Test] 2. Testing Markdown table output mode..."
MD_OUT=$(./bin/benchmark --rounds 1000 --size 256 --markdown)
echo "$MD_OUT"

if ! echo "$MD_OUT" | grep -q "SysV Shared Memory + Sem"; then
    echo "FAILED: Benchmark output missing Shared Memory!"
    exit 1
fi

if ! echo "$MD_OUT" | grep -q "TCP Loopback"; then
    echo "FAILED: Benchmark output missing TCP Loopback!"
    exit 1
fi

echo "=========================================================="
echo ">>> SUCCESS: Task 6 Benchmark Suite Test Passed!       <<<"
echo "=========================================================="

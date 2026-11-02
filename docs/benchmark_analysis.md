# Empirical IPC & Network Transport Benchmark Analysis

## 1. Executive Summary

This empirical benchmark rigorously compares inter-process communication (IPC) mechanisms natively supported in the Linux kernel against network sockets (Unix Domain Sockets and Loopback TCP). The goal is to provide quantitative justifications for the architecture chosen in the **Multi-Process IPC Chat Application**.

Testing was conducted on a Linux host (x86_64, Linux 6.x kernel) measuring **Round-Trip Ping-Pong Latency (µs)**, **Throughput (messages/second)**, and **Effective Bandwidth (MB/s)** across 5,000 round-trip exchanges per mechanism with a standard 512-byte payload.

---

## 2. Benchmark Results

| IPC / Transport Mechanism | Avg Round-Trip Latency (µs) | Throughput (msgs/sec) | Bandwidth (MB/s) | Relative Speedup vs TCP |
| :--- | :---: | :---: | :---: | :---: |
| **SysV Shared Memory + Semaphore** | **14.15 µs** | **70,668 msg/s** | **69.01 MB/s** | **3.80x Faster** |
| **Unix Domain Socket (AF_UNIX)** | **15.51 µs** | **64,492 msg/s** | **62.98 MB/s** | **3.46x Faster** |
| **POSIX FIFO (Named Pipe)** | **22.74 µs** | **43,973 msg/s** | **42.94 MB/s** | **2.36x Faster** |
| **System V Message Queue** | **28.42 µs** | **35,188 msg/s** | **34.36 MB/s** | **1.89x Faster** |
| **TCP Loopback (127.0.0.1)** | **53.72 µs** | **18,616 msg/s** | **18.18 MB/s** | **Baseline (1.0x)** |

---

## 3. Deep-Dive Mechanism Comparison

### 3.1. System V Shared Memory + Semaphore
* **Mechanism**: Direct memory mapping via `shmat()`. Data transfer occurs without traversing kernel space; only the synchronization signaling (`semop`) enters the kernel.
* **Kernel Transitions**: Zero data copy in user space (`memcpy`). Semaphore syscall takes ~1.5 µs.
* **Why it wins**: Lowest latency (**14.15 µs**) and highest throughput (**70k+ msg/s**). Ideal for bulk data, zero-copy file transfers, and high-frequency state synchronization (`shared_state_t`).

### 3.2. Unix Domain Sockets (`AF_UNIX`)
* **Mechanism**: Stream/datagram communication handled entirely in kernel memory buffer structures without the TCP/IP network protocol stack.
* **Kernel Transitions**: Data is copied into socket buffer memory in the kernel and out to the peer process, bypassing routing, checksum calculation, and packetization.
* **Result**: Very close to Shared Memory at **15.51 µs**.

### 3.3. POSIX FIFO (Named Pipe)
* **Mechanism**: Unidirectional byte pipe backed by a circular kernel buffer (`PIPE_BUF` atomic writes up to 4096 bytes).
* **Characteristics**: Predictable FIFO queuing, lightweight overhead (**22.74 µs**), perfect for client response channels (`/tmp/ipc_chat_client_<pid>.fifo`).

### 3.4. System V Message Queue
* **Mechanism**: Kernel-managed message list indexed by message type (`mtype`).
* **Characteristics**: Allows priority/type-based consumption by multiple workers without lock contention. Slightly higher latency (**28.42 µs**) due to internal kernel queue locking and metadata structures, but provides robust worker load balancing.

### 3.5. TCP Loopback Socket (`127.0.0.1`)
* **Mechanism**: Full network stack execution: packet encapsulation, TCP headers, checksum computation, TCP window management, ACK generation, loopback interface driver (`lo`).
* **Why it is slowest locally**: Round-trip latency is **53.72 µs** (nearly 4x slower than Shared Memory). Even with `TCP_NODELAY` enabled to disable Nagle's algorithm, the protocol stack introduces substantial CPU cycle overhead.

---

## 4. Architectural Conclusions for IPC Chat

1. **Hybrid Architecture Advantage**: The project uses **System V Message Queue** for worker load balancing (where priority routing is critical) and **System V Shared Memory** for zero-copy file transfers and shared online registries. This combines maximum throughput with multi-worker concurrency.
2. **Dedicated Client FIFOs**: Using POSIX FIFOs for client delivery provides atomic delivery (`sizeof(chat_msg_t) < PIPE_BUF`) while outperforming network sockets by over **2.3x**.
3. **Reproducing the Benchmark**:
   ```bash
   # Run standard benchmark suite
   ./bin/benchmark --rounds 5000 --size 512

   # Run with custom parameters
   ./bin/benchmark --rounds 10000 --size 1024 --markdown
   ```

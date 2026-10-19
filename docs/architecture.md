# Kiến Trúc Hệ Thống: Multi-Process Chat IPC trên Linux

Tài liệu này mô tả chi tiết kiến trúc kỹ thuật của hệ thống Chat Đa Tiến Trình (Multi-Process Chat) sử dụng các cơ chế Giao tiếp Liên tiến trình (IPC) cục bộ trên hệ điều hành Linux bằng ngôn ngữ C.

---

## 1. Sơ đồ kiến trúc tổng thể

```mermaid
graph TD
    subgraph Clients ["Các Tiến Trình Client"]
        C1["Client 1 (PID A)"]
        C2["Client 2 (PID B)"]
        CN["Client N (PID N)"]
    end

    subgraph FIFO_Layer ["Tầng FIFO (Named Pipes)"]
        S_FIFO["/tmp/ipc_chat_srv.fifo<br/>(Server Input FIFO)"]
        C1_FIFO["/tmp/ipc_chat_cli_A.fifo"]
        C2_FIFO["/tmp/ipc_chat_cli_B.fifo"]
        CN_FIFO["/tmp/ipc_chat_cli_N.fifo"]
    end

    subgraph Server_Dispatcher ["Tiến Trình Server Điều Phối"]
        DISP["Server Dispatcher<br/>- Đọc request từ Server FIFO<br/>- Đóng gói worker_msg_t<br/>- msgsnd() vào Message Queue"]
    end

    subgraph Msg_Queue ["System V Message Queue"]
        MQ["Message Queue (msqid)<br/>Kênh phân phối tác vụ không nghẽn"]
    end

    subgraph Server_Workers ["Các Tiến Trình Worker (fork)"]
        W1["Worker 1 (PID W1)<br/>msgrcv() & xử lý"]
        W2["Worker 2 (PID W2)<br/>msgrcv() & xử lý"]
    end

    subgraph Shared_State ["Dữ Liệu Chung & Đồng Bộ"]
        SEM["System V Semaphore<br/>Binary Mutex (semop P/V, SEM_UNDO)"]
        SHM["System V Shared Memory<br/>Bảng trạng thái Client (shared_state_t)"]
    end

    %% Flow connections
    C1 -->|Gửi request| S_FIFO
    C2 -->|Gửi request| S_FIFO
    CN -->|Gửi request| S_FIFO

    S_FIFO --> DISP
    DISP -->|msgsnd| MQ

    MQ -->|msgrcv| W1
    MQ -->|msgrcv| W2

    W1 <-->|Khóa / Mở khóa| SEM
    W2 <-->|Khóa / Mở khóa| SEM

    W1 <-->|Đọc / Ghi trạng thái| SHM
    W2 <-->|Đọc / Ghi trạng thái| SHM

    W1 -.->|Phản hồi / Broadcast| C1_FIFO
    W1 -.->|Phản hồi / Broadcast| C2_FIFO
    W2 -.->|Phản hồi / Broadcast| CN_FIFO

    C1_FIFO --> C1
    C2_FIFO --> C2
    CN_FIFO --> CN
```

---

## 2. Vai trò chi tiết của 4 cơ chế IPC

| Cơ chế IPC | File triển khai | Vai trò kỹ thuật trong hệ thống |
|---|---|---|
| **FIFO (Named Pipe)** | `src/fifo_ipc.c` | - **Server FIFO** (`/tmp/ipc_chat_srv.fifo`): Tiếp nhận mọi yêu cầu đăng ký, tin nhắn và lệnh từ các Client.<br/>- **Client FIFO** (`/tmp/ipc_chat_cli_<pid>.fifo`): Kênh nhận phản hồi và tin nhắn riêng biệt cho từng Client, tránh xung đột dữ liệu giữa các Client.<br/>- **Tính nguyên tử**: Kích thước gói tin `chat_msg_t` (~700 bytes) được thiết kế nhỏ hơn `PIPE_BUF` (4096 bytes trên Linux) đảm bảo thao tác ghi vào Server FIFO là nguyên tử (atomic), không bị trộn lẫn dữ liệu giữa nhiều client ghi đồng thời. |
| **System V Message Queue** | `src/msg_queue.c` | - Tách biệt tầng tiếp nhận (Dispatcher) và tầng xử lý (Worker).<br/>- Dispatcher không phải xử lý logic nặng mà chỉ cần `msgsnd()` vào queue rồi quay lại đọc FIFO ngay, đảm bảo thông lượng cao và không nghẽn cổ chai.<br/>- Các Worker cạnh tranh nhận việc qua `msgrcv()`, tự động cân bằng tải. |
| **System V Shared Memory** | `src/shared_memory.c` | - Lưu trữ cấu trúc `shared_state_t` bao gồm bảng danh sách các Client (`client_info_t`), trạng thái online/offline, nickname, PID và đường dẫn FIFO riêng.<br/>- Cho phép tất cả các Worker truy xuất thông tin người dùng với độ trễ cực thấp (zero-copy trong RAM). |
| **System V Semaphore** | `src/semaphore.c` | - Semaphore nhị phân (Binary Mutex) khởi tạo giá trị 1.<br/>- Bảo vệ vùng Shared Memory khi nhiều Worker cùng cập nhật danh sách client (đăng ký mới, đăng xuất, dọn dẹp).<br/>- Sử dụng cờ `SEM_UNDO` của kernel Linux: nếu một tiến trình Worker bị crash đột ngột khi đang giữ lock, hệ điều hành tự động hoàn tác (release lock), ngăn chặn hoàn toàn nguy cơ deadlock vĩnh viễn. |

---

## 3. Quản lý vòng đời tiến trình & Tín hiệu (Signals)

Hệ thống kết hợp các system call quản trị tiến trình chuẩn của Linux POSIX:

1. **`fork()`**: Server tạo ra `WORKER_COUNT` (mặc định 2) tiến trình Worker con lúc khởi động.
2. **`waitpid()` & `SIGCHLD`**:
   - Server đăng ký handler cho tín hiệu `SIGCHLD` với cờ `SA_RESTART | SA_NOCLDSTOP`.
   - Vòng lặp `while (waitpid(-1, NULL, WNOHANG) > 0)` thu hồi các tiến trình con kết thúc để tránh tạo ra tiến trình ma (zombie process).
3. **`SIGINT` / `SIGTERM`**:
   - Khi nhận tín hiệu tắt (Ctrl+C hoặc lệnh `kill`):
     1. Server phát thông báo shutdown tới toàn bộ client online qua FIFO.
     2. Gửi tín hiệu `SIGTERM` tới tất cả các PID của Worker và đợi thu hồi (`waitpid`).
     3. Đóng và xóa Server FIFO (`unlink`).
     4. Hủy Message Queue (`msgctl IPC_RMID`).
     5. Tách và hủy Shared Memory (`shmdt`, `shmctl IPC_RMID`).
     6. Hủy Semaphore (`semctl IPC_RMID`).
4. **Phát hiện Client chết đột ngột (Orphan Client Detection)**:
   - Trong quá trình phát tán tin nhắn hoặc duyệt danh sách `/list`, Worker sử dụng system call `kill(pid, 0)`.
   - Nếu trả về `-1` với lỗi `ESRCH` (No such process), Worker nhận diện Client đã bị kill đột ngột, tự động xóa slot trong Shared Memory và giảm biến đếm số lượng người online.
   - Khi ghi vào Client FIFO nếu gặp `EPIPE` (Broken Pipe), hệ thống cũng tự động đánh dấu Client offline.

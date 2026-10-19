# Đồ Án Lập Trình Hệ Thống: Multi-Process Chat sử dụng IPC trên Linux bằng C

Hệ thống Chat Đa Tiến Trình (Multi-Process Chat) trên môi trường Linux kết hợp 4 cơ chế Giao tiếp Liên tiến trình (IPC) cục bộ: **FIFO (Named Pipe)**, **System V Message Queue**, **System V Shared Memory**, và **System V Semaphore**, cùng kỹ thuật quản lý vòng đời tiến trình (`fork`, `waitpid`, `sigaction`).

---

## 1. Cấu trúc thư mục mã nguồn

```
IPC-interaction/
├── Makefile                # Kịch bản biên dịch (make all, make test, make clean)
├── README.md               # Hướng dẫn tổng quan và sử dụng
├── include/
│   ├── protocol.h          # Định nghĩa cấu trúc gói tin, mã trạng thái, hằng số IPC
│   ├── fifo_ipc.h          # Wrapper thao tác Named Pipe (FIFO)
│   ├── msg_queue.h         # Wrapper System V Message Queue
│   ├── shared_memory.h     # Wrapper System V Shared Memory
│   ├── semaphore.h         # Wrapper System V Semaphore (Binary Mutex)
│   ├── worker.h            # Định nghĩa hàm thực thi của Worker
│   └── logger.h            # Module ghi log kèm PID, timestamp và màu ANSI
├── src/
│   ├── server.c            # Tiến trình Server Dispatcher
│   ├── client.c            # Tiến trình Client giao diện terminal
│   ├── worker.c            # Tiến trình Worker xử lý nghiệp vụ chat
│   ├── fifo_ipc.c          # Triển khai FIFO IPC
│   ├── msg_queue.c         # Triển khai Message Queue IPC
│   ├── shared_memory.c     # Triển khai Shared Memory IPC
│   ├── semaphore.c         # Triển khai Semaphore IPC
│   └── logger.c            # Triển khai Logger
├── tests/
│   ├── test_basic.sh       # Kiểm thử chức năng toàn diện
│   └── test_concurrent.sh  # Kiểm thử tải và đồng thời (Stress Test)
├── docs/
│   ├── architecture.md     # Tài liệu kiến trúc và sơ đồ hệ thống
│   ├── protocol.md         # Đặc tả giao thức và cấu trúc gói tin
│   └── test_report.md      # Báo cáo kết quả kiểm thử
└── logs/                   # Thư mục chứa log hệ thống
```

---

## 2. Bảng phân công 4 cơ chế IPC

| Cơ chế IPC | Đường dẫn tài nguyên | Vai trò trong hệ thống |
|---|---|---|
| **FIFO (Named Pipe)** | `/tmp/ipc_chat_srv.fifo`<br/>`/tmp/ipc_chat_cli_<pid>.fifo` | - Server FIFO: tiếp nhận yêu cầu từ các client.<br/>- Client FIFO: kênh phản hồi riêng biệt cho từng client.<br/>- Đảm bảo ghi nguyên tử (atomic) do payload < `PIPE_BUF` (4096 bytes). |
| **Message Queue** | System V Queue (`msqid`) | - Server Dispatcher chuyển yêu cầu vào Queue bằng `msgsnd()`.<br/>- Các Worker đọc yêu cầu bằng `msgrcv()`, cân bằng tải tự động. |
| **Shared Memory** | System V Shm (`shmid`) | - Lưu trữ bảng trạng thái người dùng (`shared_state_t`), danh sách client online, nickname, PID, response FIFO. |
| **Semaphore** | System V Sem (`semid`) | - Khóa nhị phân (Binary Mutex) bảo vệ Shared Memory tránh xung đột (race condition), tích hợp cờ `SEM_UNDO` chống deadlock khi tiến trình bị kill. |

---

## 3. Hướng dẫn biên dịch và cài đặt

Yêu cầu môi trường: Linux (Ubuntu 20.04+), trình biên dịch `gcc`, thư viện POSIX threads (`-pthread`).

### Biên dịch toàn bộ:
```bash
make clean
make all
```
Sau khi biên dịch, các file thực thi sẽ nằm trong thư mục `bin/`:
- `bin/server`
- `bin/client`

### Dọn dẹp tài nguyên và file tạm:
```bash
make clean        # Xóa file nhị phân và FIFO tạm
make clean-ipc    # Quét và giải phóng toàn bộ IPC resources còn sót lại
```

---

## 4. Hướng dẫn chạy chương trình

### Bước 1: Khởi động Server
Mở một cửa sổ Terminal:
```bash
./bin/server
```
Server sẽ tự động tạo FIFO, khởi tạo Message Queue, Semaphore, Shared Memory, và `fork()` ra 2 Worker tiến trình con sẵn sàng xử lý.

### Bước 2: Khởi động Client
Mở các cửa sổ Terminal khác để tham gia chat:
```bash
./bin/client Alice
```
Hoặc:
```bash
./bin/client Bob
```

---

## 5. Danh sách lệnh trong phòng chat

| Lệnh | Ý nghĩa | Ví dụ |
|---|---|---|
| `<nội dung tin>` | Gửi tin nhắn công khai tới toàn bộ phòng chat | `Xin chào tất cả mọi người!` |
| `/msg <nick/id> <tin nhắn>` | Gửi tin nhắn riêng tư tới một người | `/msg Bob Chào bạn nhé`<br/>`/msg 2 Chào bạn nhé` |
| `/list` | Xem danh sách tất cả người dùng đang online | `/list` |
| `/help` | Hiển thị bảng hướng dẫn lệnh | `/help` |
| `/quit` | Ngắt kết nối và thoát khỏi phòng chat | `/quit` |

---

## 6. Chạy bộ kiểm thử tự động (Test Suite)

Dự án cung cấp bộ script kiểm thử tự động toàn diện:

```bash
make test
```
Lệnh này sẽ tự động chạy:
1. `tests/test_basic.sh`: Kiểm tra kết nối, chat broadcast, chat riêng theo nick, chat riêng theo ID, xử lý nickname trùng, và thoát an toàn.
2. `tests/test_concurrent.sh`: Chạy kiểm thử đa tiến trình với nhiều Client gửi đồng thời, đo thời gian xử lý và kiểm tra tính toàn vẹn của Shared Memory.

---

## 7. Các công cụ giám sát IPC trên Linux

Trong khi chương trình đang chạy, bạn có thể kiểm tra trực tiếp tài nguyên do hệ điều hành Linux quản lý:

```bash
# Kiểm tra danh sách tiến trình Server và Worker
ps -ef | grep -E "server|worker"

# Kiểm tra Message Queue
ipcs -q

# Kiểm tra Shared Memory
ipcs -m

# Kiểm tra Semaphore
ipcs -s

# Kiểm tra các Named Pipe (FIFO) đang mở
ls -l /tmp/ipc_chat_*.fifo
```

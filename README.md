# Đồ Án Lập Trình Hệ Thống: Multi-Process Chat sử dụng IPC trên Linux bằng C

Hệ thống Chat Đa Tiến Trình (Multi-Process Chat) trên môi trường Linux kết hợp 4 cơ chế Giao tiếp Liên tiến trình (IPC) cục bộ: **FIFO (Named Pipe)**, **System V Message Queue**, **System V Shared Memory**, và **System V Semaphore**, cùng kỹ thuật quản lý vòng đời tiến trình (`fork`, `waitpid`, `sigaction`).

---

## 1. Cấu trúc thư mục mã nguồn

```
IPC-interaction/
├── Makefile                    # Kịch bản biên dịch (all, test, clean, clean-ipc)
├── README.md                   # Hướng dẫn tổng quan và sử dụng hệ thống
├── SETUP_GUIDE.md              # Hướng dẫn cài đặt chi tiết trên các distro Linux
├── include/
│   ├── protocol.h              # Định nghĩa cấu trúc gói tin, mã trạng thái, hằng số IPC
│   ├── fifo_ipc.h              # Wrapper thao tác Named Pipe (FIFO)
│   ├── msg_queue.h             # Wrapper System V Message Queue
│   ├── shared_memory.h         # Wrapper System V Shared Memory
│   ├── semaphore.h             # Wrapper System V Semaphore (Binary Mutex)
│   ├── worker.h                # Định nghĩa hàm thực thi và quản lý Worker
│   └── logger.h                # Module ghi log kèm PID, timestamp và màu ANSI
├── src/
│   ├── server.c                # Tiến trình Server Dispatcher & Dynamic Auto-Scaler
│   ├── client.c                # Tiến trình Client giao diện dòng lệnh (CLI/Scriptable)
│   ├── client_tui.c            # Tiến trình Client giao diện TUI đồ họa terminal (Pure ANSI)
│   ├── monitor.c               # Bảng điều khiển giám sát thời gian thực (ipc-top)
│   ├── benchmark.c             # Bộ công cụ đo hiệu năng IPC vs Unix/TCP Sockets
│   ├── worker.c                # Tiến trình Worker xử lý nghiệp vụ chat và phòng
│   ├── fifo_ipc.c              # Triển khai FIFO IPC
│   ├── msg_queue.c             # Triển khai Message Queue IPC
│   ├── shared_memory.c         # Triển khai Shared Memory IPC
│   ├── semaphore.c             # Triển khai Semaphore IPC
│   └── logger.c                # Triển khai Logger
├── tests/
│   ├── test_basic.sh           # Kiểm thử chức năng chat cơ bản
│   ├── test_concurrent.sh      # Kiểm thử tải đồng thời (Stress Test)
│   ├── test_monitor.sh         # Kiểm thử Dashboard viễn thám IPC
│   ├── test_rooms.sh           # Kiểm thử phân luồng đa kênh (Multi-Room)
│   ├── test_autoscaling.sh     # Kiểm thử Worker Pool co giãn tự động
│   ├── test_file_transfer.sh   # Kiểm thử truyền file Zero-Copy qua SHM
│   ├── test_client_tui.sh      # Kiểm thử giao diện Terminal TUI
│   └── test_benchmark.sh       # Kiểm thử bộ đo hiệu năng chuẩn
├── docs/
│   ├── architecture.md         # Tài liệu kiến trúc và sơ đồ hệ thống
│   ├── protocol.md             # Đặc tả giao thức và cấu trúc gói tin
│   ├── benchmark_analysis.md   # Báo cáo thực nghiệm hiệu năng IPC vs Sockets
│   └── test_report.md          # Báo cáo kết quả kiểm thử
└── logs/                       # Thư mục chứa log hệ thống
```

---

## 2. Bảng phân công các cơ chế IPC

| Cơ chế IPC | Đường dẫn tài nguyên | Vai trò trong hệ thống |
|---|---|---|
| **FIFO (Named Pipe)** | `/tmp/ipc_chat_srv.fifo`<br/>`/tmp/ipc_chat_cli_<pid>.fifo` | - Server FIFO: tiếp nhận yêu cầu từ các client.<br/>- Client FIFO: kênh phản hồi riêng biệt cho từng client.<br/>- Đảm bảo ghi nguyên tử (atomic) do payload < `PIPE_BUF` (4096 bytes). |
| **Message Queue** | System V Queue (`msqid`) | - Server Dispatcher chuyển yêu cầu vào Queue bằng `msgsnd()`.<br/>- Các Worker đọc yêu cầu bằng `msgrcv()`, cân bằng tải tự động.<br/>- Đo chiều sâu hàng đợi (`msg_qnum`) phục vụ tự động tăng giảm Worker. |
| **Shared Memory** | System V Shm (`shmid`) | - Lưu trữ bảng trạng thái người dùng (`shared_state_t`), danh sách client online, nickname, phòng hiện tại, PID, response FIFO.<br/>- **Zero-Copy File Transfer**: Cấp phát phân đoạn SHM tạm để truyền file trực tiếp giữa 2 tiến trình mà không tốn dung lượng trung gian. |
| **Semaphore** | System V Sem (`semid`) | - Khóa nhị phân (Binary Mutex) bảo vệ Shared Memory tránh xung đột (race condition), tích hợp cờ `SEM_UNDO` chống deadlock khi tiến trình bị kill. |

---

## 3. Hướng dẫn biên dịch và cài đặt

> [!TIP]
> **Xem hướng dẫn chi tiết từng bước cài đặt cho máy mới (Ubuntu, Fedora, Arch Linux, WSL2):** [SETUP_GUIDE.md](SETUP_GUIDE.md)

Yêu cầu môi trường: Linux (Ubuntu 20.04+, Fedora 38+, Arch), trình biên dịch `gcc`, thư viện POSIX threads (`-pthread`).

### Biên dịch toàn bộ:
```bash
make clean
make all
```
Sau khi biên dịch, các file thực thi trong thư mục `bin/` bao gồm:
- `bin/server`: Server điều phối đa tiến trình với worker pool tự co giãn
- `bin/client`: Client giao diện dòng lệnh tiêu chuẩn (dễ script/tự động hóa)
- `bin/client_tui`: Client giao diện phân chia khung hình chuyên nghiệp (TUI Text User Interface)
- `bin/ipc_monitor`: Bảng giám sát viễn thám IPC thời gian thực (`ipc-top`)
- `bin/benchmark`: Bộ công cụ đo đạc kiểm chuẩn hiệu năng IPC vs Sockets

### Dọn dẹp tài nguyên và file tạm:
```bash
make clean        # Xóa file nhị phân và FIFO tạm
make clean-ipc    # Quét và giải phóng toàn bộ IPC resources còn sót lại
```

---

## 4. Hướng dẫn chạy chương trình

### Bước 1: Khởi động Server
```bash
./bin/server
```
Server sẽ tự động tạo FIFO, khởi tạo Message Queue, Semaphore, Shared Memory, và `fork()` ra 2 Worker tiến trình con sẵn sàng xử lý, đồng thời tự động mở rộng tối đa lên 8 Worker khi tải tăng cao.

### Bước 2: Bật Bảng Giám Sát IPC Viễn Thám (Tùy chọn)
Mở một terminal riêng biệt và chạy:
```bash
./bin/ipc_monitor
```
Hiển thị giao diện dashboard thời gian thực: số lượng client online, trạng thái worker pool, độ sâu Message Queue, trạng thái khóa Semaphore.

### Bước 3: Khởi động Client
Bạn có thể lựa chọn 1 trong 2 loại client:

**Cách 1: Giao diện Terminal TUI (Khuyên dùng)**
```bash
./bin/client_tui Alice
```
Giao diện phân chia 3 khung: Khung chat lịch sử bên trái, danh sách người dùng online bên phải, thanh trạng thái và dòng nhập liệu giữ cố định ở đáy màn hình.

**Cách 2: Giao diện Dòng lệnh Đơn giản (CLI)**
```bash
./bin/client Bob
```

---

## 5. Danh sách lệnh trong phòng chat

| Lệnh | Ý nghĩa | Ví dụ |
|---|---|---|
| `<nội dung tin>` | Gửi tin nhắn công khai tới kênh/phòng hiện tại | `Xin chào mọi người trong phòng!` |
| `/rooms` | Xem danh sách các phòng/kênh chat đang hoạt động | `/rooms` |
| `/join <#room>` | Chuyển hoặc tạo phòng chat mới (mặc định: `#general`) | `/join #linux`<br/>`/join #dev` |
| `/leave` | Rời phòng hiện tại và quay về phòng mặc định `#general` | `/leave` |
| `/msg <nick/id> <tin nhắn>` | Gửi tin nhắn riêng tư tới một người | `/msg Bob Chào bạn nhé`<br/>`/msg 2 Chào bạn nhé` |
| `/sendfile <nick> <path>` | Gửi file siêu tốc không sao chép (Zero-Copy) qua Shared Memory | `/sendfile Bob report.pdf` |
| `/list` | Xem danh sách tất cả người dùng đang online kèm phòng hiện tại | `/list` |
| `/help` | Hiển thị bảng hướng dẫn lệnh | `/help` |
| `/quit` | Ngắt kết nối và thoát khỏi phòng chat | `/quit` |

---

## 6. Chạy bộ kiểm thử tự động toàn diện (Test Suite)

Dự án cung cấp 8 kịch bản kiểm thử tự động bao phủ 100% chức năng:

```bash
make test
```
Lệnh trên sẽ lần lượt thực thi:
1. `tests/test_basic.sh`: Kiểm tra kết nối, chat broadcast, chat riêng theo nick/ID, xử lý nickname trùng, và thoát an toàn.
2. `tests/test_concurrent.sh`: Chạy kiểm thử đa tiến trình với nhiều Client gửi đồng thời (Stress Test).
3. `tests/test_monitor.sh`: Kiểm tra đo lường viễn thám `ipc_monitor`.
4. `tests/test_rooms.sh`: Kiểm tra cách ly thông điệp giữa các phòng chat độc lập.
5. `tests/test_autoscaling.sh`: Kiểm tra cơ chế tự động mở rộng Worker Pool khi hàng đợi tin nhắn bị dồn ứ.
6. `tests/test_file_transfer.sh`: Kiểm tra gửi nhận file Zero-Copy qua SHM và so khớp mã băm MD5 toàn vẹn.
7. `tests/test_client_tui.sh`: Kiểm tra giao diện đồ họa phân chia khung hình TUI.
8. `tests/test_benchmark.sh`: Kiểm tra bộ đo hiệu năng chuẩn.

---

## 7. Thực nghiệm Hiệu năng (Benchmark: IPC vs Sockets)

Xem báo cáo chi tiết tại: [docs/benchmark_analysis.md](docs/benchmark_analysis.md).

Chạy trực tiếp trên máy của bạn:
```bash
./bin/benchmark --rounds 5000 --size 512
```

### Kết quả đo thực tế trên Linux:
- **Shared Memory + Semaphore**: **14.15 µs** round-trip (**70,668 msg/s**) -> **Nhanh gấp 3.8 lần so với TCP Socket**.
- **Unix Domain Socket**: **15.51 µs** round-trip (**64,492 msg/s**).
- **POSIX FIFO**: **22.74 µs** round-trip (**43,973 msg/s**).
- **Message Queue**: **28.42 µs** round-trip (**35,188 msg/s**).
- **TCP Loopback (127.0.0.1)**: **53.72 µs** round-trip (**18,616 msg/s**).

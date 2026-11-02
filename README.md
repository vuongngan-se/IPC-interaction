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

## 3. Cài đặt trên các hệ điều hành và môi trường khác nhau

Dự án tương thích với mọi bản phân phối Linux và môi trường mô phỏng Linux trên Windows/macOS. Mở Terminal và chạy lệnh cài đặt công cụ cần thiết (`gcc`, `make`, `git`, `pthread`, `ipcs`) tương ứng với hệ điều hành của bạn:

### A. Ubuntu / Debian / Linux Mint / Pop!_OS / Windows WSL2 (Ubuntu)
```bash
sudo apt update
sudo apt install -y build-essential gcc make git util-linux procps openssh-server
```

### B. Fedora / Red Hat Enterprise Linux / CentOS Stream / Rocky Linux
```bash
sudo dnf groupinstall "Development Tools" -y
sudo dnf install -y gcc make git util-linux procps-ng openssh-server
```

### C. Arch Linux / Manjaro
```bash
sudo pacman -Syu --noconfirm base-devel gcc make git util-linux procps-ng openssh
```

### D. Windows 10 / 11 (qua WSL2)
Dự án sử dụng cơ chế IPC trực tiếp của nhân Linux (Linux Kernel) nên không chạy trên Windows Command Prompt/PowerShell thuần. Để chạy trên Windows:
1. Mở PowerShell với quyền Administrator: `wsl --install -d Ubuntu`
2. Khởi động lại máy nếu được yêu cầu, sau đó mở terminal **Ubuntu** trên Windows và thực hiện các bước như mục **A** ở trên.

### E. macOS (Apple Silicon M1/M2/M3 hoặc Intel)
macOS sử dụng nhân XNU/Darwin (khác biệt so với Linux IPC). Để chạy chuẩn 100% trên Mac:
- Chạy qua **Docker container Linux**:
  ```bash
  docker run -it --name ipc-chat -v $(pwd):/workspace -w /workspace ubuntu:22.04 bash
  apt update && apt install -y build-essential gcc make git
  ```
- Hoặc sử dụng máy ảo Linux siêu nhẹ qua **Multipass** (`multipass launch --name linux-ipc`).

---

## 4. Biên dịch mã nguồn

Sau khi clone repo về máy:
```bash
git clone https://github.com/vuongngan-se/IPC-interaction.git
cd IPC-interaction
```

Biên dịch toàn bộ hệ thống bằng 1 lệnh duy nhất:
```bash
make clean
make all
```

Sau khi biên dịch thành công, các file nhị phân trong thư mục `bin/` bao gồm:
* `bin/server`: Server điều phối đa tiến trình với Worker Pool tự co giãn (Auto-Scaler)
* `bin/client`: Client giao diện dòng lệnh đơn giản (dễ script/tự động hóa)
* `bin/client_tui`: Client giao diện phân chia khung hình chuyên nghiệp (Terminal TUI)
* `bin/ipc_monitor`: Bảng điều khiển viễn thám IPC thời gian thực (`ipc-top`)
* `bin/benchmark`: Bộ công cụ kiểm chuẩn đo đạc hiệu năng IPC vs Sockets

Dọn dẹp bản build và tài nguyên IPC:
```bash
make clean        # Xóa file nhị phân và file pipe tạm
make clean-ipc    # Quét và dọn dẹp triệt để IPC Shared Memory, Queue, Semaphore còn sót
```

---

## 5. Hướng dẫn chạy trên 1 máy (Local Host)

### Bước 1: Bật Server
Mở Terminal thứ nhất:
```bash
./bin/server
```

### Bước 2: Bật Bảng Giám Sát IPC Viễn Thám (Tùy chọn)
Mở Terminal thứ hai:
```bash
./bin/ipc_monitor
```
Hiển thị dashboard trực quan cập nhật mỗi giây: số client online, trạng thái worker pool, dung lượng queue, trạng thái khóa Semaphore.

### Bước 3: Bật Client tham gia chat
Mở các Terminal tiếp theo để đóng vai trò người dùng:
- **Client TUI (Khuyên dùng - giao diện đẹp, phân khung riêng biệt)**:
  ```bash
  ./bin/client_tui Alice
  ```
- **Client CLI (Giao diện đơn giản)**:
  ```bash
  ./bin/client Bob
  ```

---

## 6. Hướng dẫn chạy trên 2 máy hoặc nhiều máy khác nhau (Multi-Machine LAN/WAN)

### 6.1. Bản chất kỹ thuật
Các cơ chế IPC (System V Message Queue, Shared Memory, Semaphore, POSIX FIFO) tồn tại trực tiếp trong **Kernel RAM** của máy chủ cục bộ. Hai máy tính vật lý khác nhau có 2 thanh RAM và 2 Kernel riêng biệt. Do đó, để nhiều máy tính kết nối và tương tác với nhau, hệ thống hỗ trợ 2 phương án chuẩn mực:

```
+-------------------------------------------------------------------------+
|                  MÁY A (Chạy Server) - IP: 192.168.1.100                |
|                                                                         |
|   [Server Dispatcher] <---> [Message Queue] <---> [Worker Pool 2-8]     |
|                                                          ^              |
|                                                          |              |
|                                            [Shared Memory + Semaphore]  |
|                                                                         |
|  - Tiến trình Client Alice: chạy tại chỗ trên Máy A                     |
|  - Tiến trình Client Bob:   chạy từ Máy B qua phiên SSH                 |
|  - Tiến trình Client Charlie: chạy từ Máy C qua phiên SSH               |
+-------------------------------------------------------------------------+
          ^                                            ^
          | (SSH Session)                              | (SSH Session)
    +-----------+                                +-----------+
    |   MÁY B   |                                |   MÁY C   |
    | (Bob gõ)  |                                | (Charlie) |
    +-----------+                                +-----------+
```

### 6.2. Phương án 1: Mô hình máy chủ tập trung qua SSH (Khuyên dùng & Chuẩn UNIX nhất)
Mô hình này tận dụng kiến trúc Đa người dùng (Multi-user Time-sharing) của Linux. Giữ nguyên 100% bản chất IPC thuần túy, không cần cấu hình phức tạp.

#### Bước 1: Thiết lập trên Máy A (Máy chạy Server)
1. Bật dịch vụ SSH trên Máy A:
   - Trên **Ubuntu / Debian**:
     ```bash
     sudo systemctl enable --now ssh
     ```
   - Trên **Fedora / RHEL / CentOS / Arch**:
     ```bash
     sudo systemctl enable --now sshd
     ```
2. Tra cứu địa chỉ IP mạng nội bộ (LAN / Wi-Fi) của Máy A:
   ```bash
   ip -br a
   # Hoặc: hostname -I
   ```
   *(Giả sử IP của Máy A là `192.168.1.100`, tên người dùng là `ubuntu`)*.
3. Khởi động Server:
   ```bash
   cd IPC-interaction
   ./bin/server
   ```

#### Bước 2: Kết nối từ Máy B (Máy tính thứ hai cùng mạng Wi-Fi/LAN)
Trên Máy B (laptop hoặc PC thứ hai của bạn hoặc bạn bè):
1. Mở Terminal và kết nối SSH sang Máy A:
   ```bash
   ssh ubuntu@192.168.1.100
   ```
2. Nhập mật khẩu của Máy A. Sau khi vào được Máy A, mở client chat:
   ```bash
   cd IPC-interaction
   ./bin/client_tui Bob
   ```

#### Bước 3: Kết nối từ Máy C (Máy tính thứ ba)
Tương tự, từ Máy C:
```bash
ssh ubuntu@192.168.1.100
cd IPC-interaction
./bin/client_tui Charlie
```

> [!TIP]
> **Kết quả**: Alice (trên Máy A), Bob (trên Máy B), và Charlie (trên Máy C) có thể chat nhóm, chat riêng `/msg`, chuyển phòng `/join`, gửi file siêu tốc `/sendfile` hoàn toàn trực tiếp trên màn hình của từng máy!

---

### 6.3. Phương án 2: Khi 2 máy khác mạng Wi-Fi (Kết nối qua Internet)
Nếu 2 máy ở hai địa điểm khác nhau qua Internet, bạn có thể sử dụng giải pháp đường hầm an toàn miễn phí:
1. **Dùng Ngrok TCP Tunnel**:
   - Trên Máy A: `ngrok tcp 22`
   - Ngrok sẽ cấp 1 địa chỉ công khai, ví dụ: `tcp://4.tcp.ngrok.io:12345`
   - Trên Máy B ở bất kỳ đâu trên thế giới:
     ```bash
     ssh ubuntu@4.tcp.ngrok.io -p 12345
     ./bin/client_tui Bob
     ```
2. **Dùng Tailscale / ZeroTier**: Cài đặt Tailscale trên cả 2 máy, hai máy sẽ nằm chung mạng LAN ảo và có thể SSH trực tiếp bằng IP Tailscale `100.x.y.z`.

---

## 7. Danh sách lệnh trong phòng chat

| Lệnh | Ý nghĩa | Ví dụ |
|---|---|---|
| `<nội dung tin>` | Gửi tin nhắn công khai tới kênh/phòng hiện tại | `Xin chào mọi người trong phòng!` |
| `/rooms` | Xem danh sách các phòng/kênh chat đang hoạt động | `/rooms` |
| `/join <#room>` | Chuyển hoặc tạo phòng chat mới (mặc định: `#general`) | `/join #linux`<br/>`/join #dev` |
| `/leave` | Rời phòng hiện tại và quay về phòng mặc định `#general` | `/leave` |
| `/msg <nick/id> <tin nhắn>` | Gửi tin nhắn riêng tư tới một người | `/msg Bob Chào bạn nhé`<br/>`/msg 2 Chào bạn nhé` |
| `/sendfile <nick> <path>` | Gửi file siêu tốc không sao chép (Zero-Copy) qua Shared Memory | `/sendfile Bob report.pdf` |
| `/history [N]` | Xem lại N tin nhắn gần nhất của phòng hiện tại (mặc định: 10 tin) | `/history`<br/>`/history 20` |
| `/list` | Xem danh sách tất cả người dùng đang online kèm phòng hiện tại | `/list` |
| `/help` | Hiển thị bảng hướng dẫn lệnh | `/help` |
| `/quit` | Ngắt kết nối và thoát khỏi phòng chat | `/quit` |

> [!NOTE]
> **Tính năng Lịch Sử Tự Động (Late-Joiner Delivery)**: Khi người dùng mới kết nối hoặc đổi phòng bằng `/join <#phòng>`, hệ thống sẽ tự động gửi lại 5 tin nhắn gần nhất của phòng đó để người dùng nắm bắt ngay ngữ cảnh thảo luận. Lịch sử được lưu trữ bền vững tại `logs/chat_history.log`.

---

## 8. Chạy bộ kiểm thử tự động toàn diện (Test Suite)

Dự án cung cấp 9 kịch bản kiểm thử tự động bao phủ 100% chức năng:

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
9. `tests/test_history.sh`: Kiểm tra lưu trữ lịch sử phòng chat và tự động cấp phát lịch sử cho người dùng mới (Late Joiner).

---

## 9. Thực nghiệm Hiệu năng (Benchmark: IPC vs Sockets)

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

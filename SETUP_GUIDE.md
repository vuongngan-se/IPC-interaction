# Hướng Dẫn Cài Đặt và Triển Khai Hệ Thống IPC Chat (Setup & Installation Guide)

Tài liệu này hướng dẫn chi tiết từng bước cài đặt, cấu hình môi trường, cài đặt thư viện và chạy dự án **Multi-Process IPC Chat** trên một máy tính Linux mới từ đầu (Ubuntu, Debian, Fedora, Arch Linux, hoặc Windows WSL2).

---

## 1. Yêu cầu hệ thống

- **Hệ điều hành**: Linux (Kernel 3.x trở lên, khuyên dùng Kernel 5.x / 6.x)
- **Kiến trúc CPU**: x86_64 (Intel/AMD) hoặc ARM64 (Raspberry Pi, Apple Silicon chạy Linux VM)
- **Môi trường hỗ trợ**:
  - Máy tính cài trực tiếp Ubuntu / Debian / Fedora / Arch Linux
  - Máy ảo (VirtualBox, VMware, KVM, Multipass)
  - **Windows 10/11**: Chạy qua **WSL2** (Windows Subsystem for Linux - Ubuntu)

> [!NOTE]
> Dự án sử dụng các cơ chế IPC cục bộ của Linux Kernel (System V IPC, POSIX FIFO, POSIX Threads). Do đó, chương trình **không chạy trực tiếp trên Windows thuần** (Command Prompt/PowerShell) mà bắt buộc chạy trong môi trường Linux hoặc WSL2.

---

## 2. Cài đặt các công cụ & thư viện cần thiết

Mở Terminal trên máy Linux mới và chạy các lệnh cài đặt tương ứng với bản phân phối bạn đang dùng:

### A. Dành cho Ubuntu / Debian / Linux Mint / Pop!_OS / WSL2 (Khuyên dùng)
```bash
# 1. Cập nhật danh sách gói phần mềm
sudo apt update

# 2. Cài đặt bộ công cụ phát triển, trình biên dịch GCC, Make và Git
sudo apt install -y build-essential gcc make git util-linux procps
```

### B. Dành cho Fedora / Red Hat / CentOS / Rocky Linux
```bash
sudo dnf groupinstall "Development Tools" -y
sudo dnf install -y gcc make git util-linux procps-ng
```

### C. Dành cho Arch Linux / Manjaro
```bash
sudo pacman -Syu --noconfirm base-devel gcc make git util-linux procps-ng
```

---

## 3. Kiểm tra môi trường sau khi cài đặt

Đảm bảo các công cụ đã được cài đặt thành công bằng cách kiểm tra phiên bản:

```bash
gcc --version   # Phải hiển thị GCC (khuyên dùng GCC 9.x trở lên)
make --version  # Công cụ GNU Make
git --version   # Công cụ quản lý mã nguồn Git
ipcs -h         # Công cụ kiểm tra System V IPC
```

---

## 4. Tải mã nguồn về máy mới

Dùng lệnh Git để clone kho mã nguồn về máy:

```bash
# 1. Clone repository
git clone https://github.com/vuongngan-se/IPC-interaction.git

# 2. Chuyển vào thư mục dự án
cd IPC-interaction
```

Kiểm tra cấu trúc dự án:
```bash
ls -la
```
Bạn sẽ thấy các thư mục: `include/`, `src/`, `tests/`, `docs/`, `logs/` và file `Makefile`.

---

## 5. Biên dịch dự án

Dự án đã được cấu hình sẵn với `Makefile` tối ưu:

```bash
# Xóa bản build cũ và biên dịch lại từ đầu:
make clean
make all
```

**Quá trình biên dịch sẽ tạo ra 2 file thực thi trong thư mục `bin/`:**
- `bin/server`: Tiến trình Server điều phối và các Worker
- `bin/client`: Tiến trình Client giao diện dòng lệnh

*Cờ biên dịch sử dụng*: `-Wall -Wextra -Wpedantic -g -Iinclude -pthread` bảo đảm mã nguồn nghiêm ngặt, chuẩn mực và tối ưu trên Linux.

---

## 6. Chạy kiểm thử tự động (Verification)

Trước khi chạy tương tác thủ công, hãy chạy bộ kiểm thử tự động để xác nhận toàn bộ 4 cơ chế IPC (FIFO, Queue, Shm, Sem) hoạt động hoàn hảo trên máy mới:

```bash
make test
```

Nếu màn hình hiển thị:
```
[SUCCESS] Phase 5 All Chat Features Verified 100%!
[SUCCESS] Phase 6 Concurrency Test Passed 100%!
All tests completed and passed!
```
-> Hệ thống đã sẵn sàng 100%!

---

## 7. Hướng dẫn chạy thử nghiệm tương tác nhiều người

Để trải nghiệm giao tiếp đa tiến trình trong thực tế, bạn mở **3 cửa sổ Terminal** trong cùng thư mục `IPC-interaction`:

### Cửa sổ 1 — Khởi động Server:
```bash
./bin/server
```
*Server sẽ khởi tạo FIFO `/tmp/ipc_chat_srv.fifo`, Message Queue, Semaphore, Shared Memory và spawn 2 Worker con.*

### Cửa sổ 2 — Khởi động Client Alice:
```bash
./bin/client Alice
```

### Cửa sổ 3 — Khởi động Client Bob:
```bash
./bin/client Bob
```

### Thao tác thử nghiệm trong phòng chat:
1. **Chat nhóm công khai**:
   - Ở Terminal của Alice, gõ: `Chào cả phòng chat!` và nhấn Enter.
   - Bob sẽ nhận được tin nhắn ngay lập tức: `[Alice (#1)]: Chào cả phòng chat!`.

2. **Chat riêng tư (Private Message)**:
   - Ở Terminal của Bob, gõ: `/msg Alice Bạn có khỏe không?`
   - Chỉ duy nhất Alice nhận được tin nhắn riêng này màu tím: `[PM from Bob (#2)]: Bạn có khỏe không?`.

3. **Xem danh sách người online**:
   - Gõ: `/list`
   - Màn hình hiển thị danh sách tất cả người dùng kèm PID hệ thống.

4. **Xem menu trợ giúp**:
   - Gõ: `/help`

5. **Thoát an toàn**:
   - Gõ: `/quit` ở bất kỳ client nào.
   - Nhấn `Ctrl + C` ở Terminal Server để tắt Server sạch sẽ.

---

## 8. Các lệnh giám sát IPC trên hệ điều hành

Trong khi Server và Client đang hoạt động, bạn có thể mở một Terminal khác để "soi" trực tiếp tài nguyên kernel đang quản lý:

```bash
# 1. Xem cây tiến trình Server và các Worker con
ps -ef | grep -E "server|worker"

# 2. Xem System V Message Queue
ipcs -q

# 3. Xem System V Shared Memory
ipcs -m

# 4. Xem System V Semaphore
ipcs -s

# 5. Xem các file Named Pipe (FIFO) đang mở trong /tmp
ls -l /tmp/ipc_chat_*.fifo
```

---

## 9. Xử lý sự cố thường gặp (Troubleshooting)

### Sự cố 1: Lỗi "Address already in use" hoặc kẹt tài nguyên IPC cũ
- **Nguyên nhân**: Lần chạy trước bị tắt cưỡng bức bằng `kill -9` khiến kernel chưa kịp dọn Message Queue hoặc Shared Memory cũ.
- **Cách khắc phục**:
  ```bash
  make clean-ipc
  ```
  Lệnh này sẽ quét và giải phóng sạch sẽ mọi IPC tài nguyên còn tồn đọng.

### Sự cố 2: Lỗi không thể tạo file trong `/tmp`
- **Nguyên nhân**: Phân vùng `/tmp` bị đầy hoặc không có quyền ghi.
- **Cách khắc phục**:
  ```bash
  df -h /tmp
  sudo chmod 1777 /tmp
  ```

### Sự cố 3: Chạy trên WSL2 nhưng không thấy tiến trình
- Đảm bảo WSL2 đang sử dụng Ubuntu 64-bit và không bị chặn bởi tường lửa của bên thứ ba. Kiểm tra phiên bản bằng `wsl --status` trên PowerShell của Windows.

---

## 10. Chạy trên 2 máy hoặc nhiều máy tính khác nhau

Xem hướng dẫn chi tiết tại: **[docs/multi_machine_guide.md](docs/multi_machine_guide.md)**.

### Tóm tắt nhanh cách làm (Phương án SSH - Chuẩn Unix):
1. **Máy A (Server)**:
   - Cài SSH Server: `sudo apt install openssh-server -y`
   - Xem IP máy: `ip a | grep inet` (Ví dụ IP là `192.168.1.100`)
   - Chạy Server: `./bin/server`
2. **Máy B (Client thứ hai trong cùng mạng LAN / Wi-Fi)**:
   - Mở Terminal và SSH vào Máy A: `ssh <user>@192.168.1.100`
   - Chạy Client: `cd IPC-interaction && ./bin/client Bob`
3. **Máy C (Client thứ ba)**:
   - Tương tự Máy B: `ssh <user>@192.168.1.100` -> `./bin/client Charlie`

*Cả 2 hoặc nhiều máy sẽ cùng tham gia phòng chat trong thời gian thực mà không cần sửa đổi bất kỳ dòng mã nguồn nào!*

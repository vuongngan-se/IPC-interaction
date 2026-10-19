# Hướng Dẫn Chạy Hệ Thống IPC Chat Trên 2 Máy Hoặc Nhiều Máy

Tài liệu này giải thích bản chất kỹ thuật của giao tiếp IPC khi mở rộng ra môi trường nhiều máy tính vật lý và cung cấp các phương án triển khai thực tế chi tiết.

---

## 1. Bản chất kỹ thuật: Tại sao IPC mặc định chạy trên 1 máy?

Hệ thống của chúng ta sử dụng 4 cơ chế IPC chính:
1. **FIFO (Named Pipe)**: Tồn tại trên File System của máy chủ.
2. **System V Message Queue**: Tồn tại trong bộ nhớ Kernel của máy chủ.
3. **System V Shared Memory**: Tồn tại trực tiếp trên thanh RAM vật lý của máy chủ.
4. **System V Semaphore**: Do Kernel của máy chủ quản lý và đồng bộ.

> [!IMPORTANT]
> Hai máy tính khác nhau có **hai thanh RAM riêng biệt** và **hai Kernel Linux riêng biệt**, do đó không thể truy cập trực tiếp vào cùng một vùng Shared Memory hay System V Semaphore qua cáp mạng nếu không có cơ chế trung gian.

Để chạy hệ thống trên **2 máy hoặc nhiều máy hơn** trong mạng LAN/Internet, có 2 phương pháp chuẩn mực và hiệu quả nhất dưới đây.

---

## PHƯƠNG ÁN 1: Mô hình Máy chủ tập trung qua SSH (Khuyên dùng & Chuẩn UNIX nhất)

Đây là cách kinh điển của hệ điều hành Linux/UNIX (kiến trúc đa người dùng - Multi-user Time-sharing). 
- **Mã nguồn giữ nguyên 100%**: Không cần sửa bất kỳ dòng code nào.
- Toàn bộ cơ chế IPC (FIFO, MQ, SHM, Semaphore) hoạt động hoàn hảo trong Kernel của máy chủ.
- Người dùng ở các máy tính khác kết nối từ xa vào để mở terminal chat của riêng mình.

```
+--------------------------------------------------------------------------+
|                       MÁY A (Chạy Server) - IP: 192.168.1.100            |
|                                                                          |
|   [Server Dispatcher] <---> [Message Queue] <---> [Workers]             |
|                                                      ^                   |
|                                                      |                   |
|                                            [Shared Memory + Semaphore]   |
|                                                                          |
|  - Tiến trình Client 1 (Alice) chạy tại chỗ                              |
|  - Tiến trình Client 2 (Bob) chạy qua SSH Session từ Máy B               |
|  - Tiến trình Client 3 (Charlie) chạy qua SSH Session từ Máy C           |
+--------------------------------------------------------------------------+
          ^                                              ^
          | (SSH port 22)                                | (SSH port 22)
          |                                              |
    +-----------+                                  +-----------+
    |   MÁY B   |                                  |   MÁY C   |
    |  (Bob gõ) |                                  | (Charlie) |
    +-----------+                                  +-----------+
```

### Các bước thực hiện:

#### Bước 1: Chuẩn bị trên Máy A (Máy đóng vai trò Server)
1. Cài đặt OpenSSH Server (nếu chưa có):
   ```bash
   sudo apt install -y openssh-server
   sudo systemctl enable --now ssh
   ```
2. Xem địa chỉ IP của Máy A trong mạng LAN:
   ```bash
   ip a | grep inet
   ```
   *(Ví dụ IP của Máy A là: `192.168.1.100`, tên tài khoản là `ubuntu`)*.
3. Khởi động Server:
   ```bash
   cd IPC-interaction
   ./bin/server
   ```

#### Bước 2: Kết nối từ Máy B (Client 2)
Trên Máy B (máy của bạn bè, đồng nghiệp hoặc máy tính thứ hai trong cùng mạng Wi-Fi/LAN):
1. Mở Terminal và SSH vào Máy A:
   ```bash
   ssh ubuntu@192.168.1.100
   ```
   *(Nhập mật khẩu của Máy A)*.
2. Sau khi đăng nhập thành công vào Máy A, chạy Client với tên của mình:
   ```bash
   cd IPC-interaction
   ./bin/client Bob
   ```

#### Bước 3: Kết nối từ Máy C (Client 3)
Tương tự, từ Máy C mở Terminal:
```bash
ssh ubuntu@192.168.1.100
cd IPC-interaction
./bin/client Charlie
```

**Kết quả**: Alice (trên Máy A), Bob (trên Máy B) và Charlie (trên Máy C) có thể chat nhóm, chat riêng `/msg`, xem `/list` hoàn toàn độc lập trên từng màn hình máy tính riêng biệt!

---

## PHƯƠNG ÁN 2: Sử dụng Network Pipe Bridge qua `socat` hoặc `netcat`

Nếu bạn muốn Client thực sự được biên dịch và chạy trên Máy B, bạn có thể biến Named Pipe (FIFO) thành luồng mạng TCP bằng công cụ `socat`.

### Mô hình hoạt động:
- Máy A tạo một TCP socket chuyển tiếp dữ liệu đến Server FIFO.
- Máy B gửi tin nhắn qua TCP đến Máy A.

### Cách thực hiện bằng `socat`:

#### 1. Trên cả 2 máy, cài đặt `socat`:
```bash
sudo apt install -y socat
```

#### 2. Trên Máy A (Server):
Khởi động Server chat:
```bash
./bin/server
```
Mở một terminal khác trên Máy A để lắng nghe kết nối mạng trên cổng 9999 và chuyển thẳng vào Server FIFO:
```bash
socat TCP-LISTEN:9999,fork,reuseaddr GOPEN:/tmp/ipc_chat_srv.fifo
```

---

## PHƯƠNG ÁN 3: Mở rộng kiến trúc với module Socket Gateway (Dành cho bản Nâng cao V2)

Nếu muốn phát triển đồ án thành hệ thống quy mô lớn hỗn hợp (Hybrid IPC + Network):
1. **Kiến trúc**:
   - Server lõi vẫn dùng 4 cơ chế IPC cục bộ (FIFO, MQ, SHM, Semaphore).
   - Viết thêm một tiến trình con `gateway.c` chạy trên Server: tiến trình này lắng nghe trên cổng TCP 8080 (`socket`, `bind`, `listen`, `accept`).
   - Khi có Client từ xa gửi gói `chat_msg_t` qua TCP Socket, `gateway.c` chỉ việc ghi gói đó vào Server FIFO `/tmp/ipc_chat_srv.fifo` như một client cục bộ.
2. **Ưu điểm**:
   - Vừa thể hiện được toàn bộ 4 cơ chế IPC cục bộ nâng cao trên Server.
   - Vừa hỗ trợ các máy tính từ xa ở bất cứ đâu trong mạng Internet kết nối vào.

---

## Tóm tắt so sánh các phương án

| Tiêu chí | Phương án 1 (SSH Session) | Phương án 2 (`socat` Bridge) | Phương án 3 (Socket Gateway) |
|---|---|---|---|
| **Độ phức tạp** | Rất đơn giản, làm được ngay | Trung bình | Cần viết thêm code Gateway |
| **Sửa đổi mã nguồn** | **Không cần sửa code (0%)** | Không cần sửa code C | Cần viết thêm file `gateway.c` |
| **Giữ trọn bản chất IPC** | 100% (FIFO, MQ, SHM, Sem) | Tương đối | 100% |
| **Tính ứng dụng bảo vệ đồ án** | **Rất cao (khuyên dùng khi thuyết trình)** | Trình diễn kỹ thuật Linux | Mức đồ án tốt nghiệp |

Khi bảo vệ đồ án hoặc nộp bài, **Phương án 1 (SSH Session)** là phương án tối ưu và chuẩn xác nhất: vừa chứng minh được hệ thống phục vụ nhiều người dùng trên nhiều máy tính khác nhau, vừa giữ trọn vẹn giá trị cốt lõi của đề tài là các kỹ thuật Lập trình hệ thống Linux và IPC nội bộ.

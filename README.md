# IPC -- Giao tiếp liên tiến trình bằng TCP Socket (C / Linux Ubuntu)

## Giới thiệu

Dự án minh họa cơ chế **IPC (Inter-Process Communication)** trong Lập trình Hệ thống,
sử dụng TCP Socket trên nền POSIX (Linux Ubuntu).
Hai chương trình C riêng biệt -- server và client -- giao tiếp với nhau
thông qua giao thức TCP, trao đổi tin nhắn tương tác theo thời gian thực.

## IPC là gì?

IPC (Inter-Process Communication) là tập hợp các phương thức để hai hay nhiều
tiến trình trao đổi dữ liệu với nhau thông qua kernel của hệ điều hành.

Các cơ chế IPC phổ biến trên Linux:

| Cơ chế | Mô tả |
|---|---|
| Pipe | Kênh một chiều giữa tiến trình cha-con |
| Named Pipe (FIFO) | Pipe có tên trên hệ thống file, dùng giữa các tiến trình không liên quan |
| Message Queue | Hàng đợi tin nhắn do kernel quản lý |
| Shared Memory | Vùng nhớ chung giữa các tiến trình |
| Semaphore | Cơ chế đồng bộ hóa truy cập tài nguyên |
| Signal | Thông báo bất đồng bộ giữa các tiến trình |
| **Socket (TCP/UDP)** | Giao tiếp qua mạng hoặc trên cùng máy -- **dùng trong dự án này** |

## Cấu trúc dự án

```
.
├── server.c      # Chương trình server -- lắng nghe và phản hồi
├── client.c      # Chương trình client -- kết nối và gửi tin nhắn
└── README.md     # Tài liệu hướng dẫn (file này)
```

## System calls sử dụng

Chương trình gọi trực tiếp các system call của Linux, đây là đặc trưng
của lập trình hệ thống:

| System call | Vai trò |
|---|---|
| `socket()` | Tạo socket descriptor |
| `bind()` | Gắn socket với địa chỉ IP và port |
| `listen()` | Đưa socket vào trạng thái chờ kết nối |
| `accept()` | Chấp nhận kết nối từ client |
| `connect()` | Client kết nối tới server |
| `send()` | Gửi dữ liệu qua socket |
| `recv()` | Nhận dữ liệu từ socket |
| `close()` | Đóng socket descriptor |
| `setsockopt()` | Cấu hình tùy chọn socket (SO_REUSEADDR) |

## Yêu cầu

- Hệ điều hành: Linux Ubuntu (18.04 trở lên)
- Trình biên dịch: GCC

```bash
sudo apt update
sudo apt install gcc -y
```

## Biên dịch

```bash
gcc server.c -o server
gcc client.c -o client
```

## Cách chạy

### Bước 1 -- Khởi động server

Mở terminal thứ nhất:

```bash
./server
```

Kết quả:

```
[Server] Listening on port 8080 ...
```

Server đang lắng nghe trên port 8080, chờ client kết nối.

### Bước 2 -- Khởi động client

Mở terminal thứ hai:

```bash
./client
```

Kết quả:

```
[Client] Connected to server 127.0.0.1:8080
[Client] Enter message: _
```

Phía server sẽ hiện:

```
[Server] Client connected! (IP: 127.0.0.1, Port: xxxxx)
```

### Bước 3 -- Trao đổi tin nhắn

Client gõ tin nhắn và nhấn Enter, server nhận được rồi gõ phản hồi:

```
# Phía Client
[Client] Enter message: Xin chao server!
[Server says]: Chao ban, toi la server!

# Phía Server
[Client says]: Xin chao server!
[Server] Enter reply: Chao ban, toi la server!
```

### Bước 4 -- Kết thúc

Gõ `quit` ở bất kỳ bên nào rồi nhấn Enter:

```
# Phía Client
[Client] Enter message: quit
[Client] Quit requested. Disconnecting.
[Client] Closed.

# Phía Server
[Client says]: quit
[Server] Client requested quit. Shutting down.
[Server] Closed.
```

## Luồng hoạt động

```
  CLIENT                              SERVER
    |                                    |
    |          socket(), bind(),         |
    |          listen()                  |
    |                                    |
    |  socket()                          |
    |  connect() -------- SYN -------->  |
    |            <------ SYN-ACK ------  | accept()
    |            -------- ACK -------->  |
    |                                    |
    |  send() ------- [message] ------>  | recv()
    |                                    |
    |  recv() <------ [reply] ---------  | send()
    |                                    |
    |           ... lặp lại ...          |
    |                                    |
    |  send() ------- "quit" -------->   | recv()
    |                                    |
    |  close()                           | close()
    |                                    |
```

## Cấu hình

| Tham số | Giá trị mặc định | File | Mô tả |
|---|---|---|---|
| `PORT` | `8080` | server.c, client.c | Cổng TCP |
| `SERVER_IP` | `127.0.0.1` | client.c | Địa chỉ IP của server |
| `BUFSIZE` | `1024` | server.c, client.c | Kích thước bộ đệm (byte) |

Muốn thay đổi thì sửa giá trị `#define` trong file tương ứng rồi biên dịch lại.

## Xử lý lỗi thường gặp

| Lỗi | Nguyên nhân | Cách khắc phục |
|---|---|---|
| `Bind failed: Address already in use` | Port 8080 đang bị tiến trình khác chiếm | Đổi `PORT` hoặc chạy `sudo lsof -i :8080` để tìm và tắt tiến trình |
| `Connection refused` | Server chưa chạy hoặc sai port | Khởi động server trước, kiểm tra port |
| `Invalid address` | Sai định dạng IP trong `SERVER_IP` | Kiểm tra lại chuỗi IP |

## Hạn chế và hướng mở rộng

- Server hiện chỉ phục vụ 1 client tại một thời điểm.
  Muốn phục vụ nhiều client đồng thời có thể dùng `fork()` hoặc `pthread`.
- Tin nhắn chưa mã hóa. Có thể tích hợp TLS/SSL để bảo mật.
- Có thể mở rộng sang UDP socket để so sánh hiệu suất với TCP.

## Tài liệu tham khảo

- Beej's Guide to Network Programming:
  https://beej.us/guide/bgnet/
- Linux man pages: `man 2 socket`, `man 2 bind`, `man 2 listen`,
  `man 2 accept`, `man 2 connect`, `man 2 send`, `man 2 recv`
- W. Richard Stevens, "UNIX Network Programming, Volume 1"

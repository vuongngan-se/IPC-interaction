# IPC -- Giao tiếp liên tiến trình bằng TCP Socket (C / Linux)

Chương trình minh họa cơ chế IPC (Inter-Process Communication) trong Lập trình Hệ thống.
Hai tiến trình riêng biệt -- server và client -- giao tiếp qua TCP Socket,
trao đổi tin nhắn tương tác theo thời gian thực trên Linux Ubuntu.

## Clone về máy

```bash
git clone https://github.com/vuongngan-se/IPC-interaction.git
cd IPC-interaction
```

## Cấu trúc

```
├── server.c    # Chương trình server -- lắng nghe và phản hồi
├── client.c    # Chương trình client -- kết nối và gửi tin nhắn
└── README.md
```

## Yêu cầu

- Linux Ubuntu 18.04 trở lên
- GCC (thường có sẵn, nếu chưa có thì cài):

```bash
sudo apt update && sudo apt install gcc -y
```

## Biên dịch

```bash
gcc server.c -o server
gcc client.c -o client
```

## Chạy chương trình

Mở 2 terminal trong cùng thư mục project.

Terminal 1 -- khởi động server:

```bash
./server
```

```
[Server] Listening on port 8080 ...
```

Terminal 2 -- khởi động client:

```bash
./client
```

```
[Client] Connected to server 127.0.0.1:8080
[Client] Enter message: _
```

## Trao đổi tin nhắn

Client gõ tin nhắn, nhấn Enter. Server nhận và gõ phản hồi.

```
# Terminal 2 (Client)
[Client] Enter message: Xin chao server!
[Server says]: Chao ban, toi la server!

# Terminal 1 (Server)
[Client says]: Xin chao server!
[Server] Enter reply: Chao ban, toi la server!
```

Gõ `quit` ở bất kỳ bên nào để kết thúc.

## Cấu hình

| Tham số | Mặc định | File | Mô tả |
|---|---|---|---|
| `PORT` | `8080` | server.c, client.c | Cổng TCP |
| `SERVER_IP` | `127.0.0.1` | client.c | IP của server |
| `BUFSIZE` | `1024` | server.c, client.c | Kích thước bộ đệm |

Muốn thay đổi thì sửa `#define` trong file tương ứng rồi biên dịch lại.

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
    |  recv() <------ [reply] ---------  | send()
    |                                    |
    |           ... lặp lại ...          |
    |                                    |
    |  send() ------- "quit" -------->   | recv()
    |  close()                           | close()
```

## System calls sử dụng

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

## Lỗi thường gặp

| Lỗi | Cách khắc phục |
|---|---|
| `Bind failed: Address already in use` | Đổi `PORT` hoặc chạy `sudo lsof -i :8080` để tìm và tắt tiến trình đang chiếm port |
| `Connection refused` | Khởi động server trước khi chạy client |
| Client kết nối từ máy khác không được | Sửa `SERVER_IP` trong client.c thành IP thực của máy chạy server |

## Hạn chế

- Server chỉ phục vụ 1 client. Muốn nhiều client cần dùng `fork()` hoặc `pthread`.
- Tin nhắn chưa mã hóa. Có thể tích hợp TLS/SSL để bảo mật.

## Tham khảo

- Beej's Guide to Network Programming: https://beej.us/guide/bgnet/
- Linux man pages: `man 2 socket`, `man 2 bind`, `man 2 listen`, `man 2 accept`
- W. Richard Stevens, "UNIX Network Programming, Volume 1"

# Báo Cáo Kiểm Thử Hệ Thống (Test Report)

Báo cáo kiểm thử đồ án Multi-Process Chat IPC trên môi trường Linux Ubuntu.

---

## 1. Môi trường thực nghiệm

- **Hệ điều hành**: Linux (Ubuntu 64-bit, Kernel 6.8+)
- **Trình biên dịch**: GCC 13+ với cờ `-Wall -Wextra -Wpedantic -g -Iinclude -pthread`
- **Bộ công cụ IPC kiểm tra**: `ipcs`, `ipcrm`, `lsof`, `strace`, `valgrind`

---

## 2. Kịch bản kiểm thử chức năng (`tests/test_basic.sh`)

| STT | Tình huống kiểm thử | Hành động | Kết quả mong đợi | Kết quả thực tế | Trạng thái |
|---|---|---|---|---|---|
| 1 | Khởi tạo Server & IPC | Chạy `./bin/server` | Tạo FIFO, Message Queue, Shared Memory, Semaphore | Server khởi tạo thành công, `WORKER_COUNT=2` được spawn qua `fork()` | **PASS** |
| 2 | Đăng ký Client Alice | Client Alice kết nối | Cấp ID #1, gửi thông báo tham gia | Alice nhận ID #1, phòng nhận thông báo | **PASS** |
| 3 | Đăng ký Client Bob | Client Bob kết nối | Cấp ID #2, gửi thông báo tham gia | Bob nhận ID #2, Alice nhận thông báo Bob vào phòng | **PASS** |
| 4 | Chat nhóm công khai | Alice gửi: "Hello world from Alice!" | Bob nhận được nội dung từ Alice | Bob in ra: `[Alice (#1)]: Hello world from Alice!` | **PASS** |
| 5 | Chat riêng theo Nickname | Bob gửi: `/msg Alice Secret message from Bob` | Chỉ Alice nhận được | Alice in ra: `[PM from Bob (#2)]: Secret message from Bob` | **PASS** |
| 6 | Xem danh sách online | Alice gõ lệnh `/list` | Nhận bảng danh sách người dùng kèm PID | Alice nhận bảng `=== Online Users ===` có Alice và Bob | **PASS** |
| 7 | Chat riêng theo Client ID | Charlie gửi: `/msg 2 Hello Bob from Charlie` | Bob (ID #2) nhận được tin nhắn | Bob in: `[PM from Charlie (#3)]: Hello Bob from Charlie` | **PASS** |
| 8 | Chat riêng tới người không tồn tại | Charlie gửi: `/msg Ghost Nobody here` | Bị từ chối an toàn, nhận thông báo lỗi | Charlie nhận: `[ERROR] Target user not found or offline.` | **PASS** |
| 9 | Từ chối trùng Nickname | Client mới đăng ký tên "Alice" | Bị từ chối với mã lỗi `STATUS_ERR_NICK_TAKEN` | Bị từ chối: `Nickname 'Alice' already taken` | **PASS** |
| 10 | Thoát phòng chat | Alice, Bob, Charlie gõ `/quit` | Đánh dấu offline, xóa FIFO riêng, thông báo rời phòng | Mọi client rời phòng sạch sẽ, phòng nhận thông báo | **PASS** |
| 11 | Dừng Server bằng `SIGINT` | Gửi tín hiệu `SIGINT` (Ctrl+C) | Báo shutdown tới client, dừng Worker, dọn sạch IPC | Server đóng FIFO, hủy Queue, giải phóng Shm & Semaphore | **PASS** |

---

## 3. Kịch bản kiểm thử tải & đồng thời (`tests/test_concurrent.sh`)

- **Số lượng client đồng thời**: 6 tiến trình Client độc lập.
- **Số lượng tin nhắn**: 30 tin nhắn được gửi dồn dập đồng thời.
- **Thời gian xử lý hoàn tất**: ~4.2 giây (bao gồm cả khởi động tiến trình con và bắt tay đăng ký).
- **Hiện tượng race condition**: Không phát sinh race condition nhờ cơ chế khóa System V Semaphore và cờ `SEM_UNDO`.
- **Hiện tượng kẹt FIFO / Deadlock**: Không có hiện tượng kẹt, thông lượng đảm bảo thông suốt.

---

## 4. Xử lý lỗi ngoại lệ nâng cao

1. **Client bị kill bất thường (`kill -9`)**:
   - Khi một client bị tắt cưỡng bức bằng `kill -9`, Server/Worker kiểm tra trạng thái tiến trình qua `kill(pid, 0)`.
   - Khi phát hiện mã lỗi `ESRCH`, slot của Client ngay lập tức được thu hồi an toàn.
2. **Khởi động lại khi tồn tại tài nguyên IPC cũ**:
   - Server tự động phát hiện và khởi tạo lại trạng thái Shared Memory, thiết lập lại giá trị Semaphore về 1, tái sử dụng FIFO an toàn mà không bị crash.

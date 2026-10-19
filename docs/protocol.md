# Đặc Tả Giao Thức & Cấu Trúc Dữ Liệu (Protocol Specification)

Tài liệu này đặc tả chi tiết các loại thông điệp, cấu trúc dữ liệu, mã trạng thái và khuôn dạng gói tin trong hệ thống IPC Chat.

---

## 1. Giới hạn hệ thống

| Hằng số | Giá trị | Ý nghĩa |
|---|---|---|
| `MAX_CLIENTS` | 32 | Số lượng client tối đa kết nối đồng thời |
| `MAX_NICKNAME_LEN` | 32 | Độ dài tối đa của nickname (bao gồm `\0`) |
| `MAX_MSG_LEN` | 512 | Độ dài tối đa nội dung văn bản tin nhắn |
| `FIFO_PATH_LEN` | 128 | Độ dài tối đa đường dẫn file FIFO trong `/tmp/` |
| `SERVER_FIFO_PATH` | `"/tmp/ipc_chat_srv.fifo"` | Đường dẫn FIFO tiếp nhận của Server |
| `CLIENT_FIFO_TEMPLATE` | `"/tmp/ipc_chat_cli_%d.fifo"` | Mẫu tên FIFO riêng theo PID của Client |
| `PIPE_BUF` | 4096 bytes (Linux standard) | Ngưỡng ghi nguyên tử của Named Pipe POSIX |

---

## 2. Loại thông điệp (`MessageType`)

```c
typedef enum {
    MSG_REGISTER        = 1,    /* Client yêu cầu đăng ký phiên */
    MSG_REGISTER_RESP   = 2,    /* Server phản hồi kèm Client ID được cấp */
    MSG_CHAT_ALL        = 3,    /* Tin nhắn công khai gửi phòng chung (broadcast) */
    MSG_CHAT_PRIVATE    = 4,    /* Tin nhắn riêng tư gửi một người (/msg) */
    MSG_LIST_USERS      = 5,    /* Yêu cầu xem danh sách người dùng online (/list) */
    MSG_LIST_RESP       = 6,    /* Phản hồi danh sách người dùng online */
    MSG_DISCONNECT      = 7,    /* Client thông báo ngắt kết nối (/quit) */
    MSG_SERVER_NOTICE   = 8,    /* Thông báo hệ thống (join/leave/shutdown) */
    MSG_ERROR           = 9     /* Thông báo lỗi thao tác */
} MessageType;
```

---

## 3. Mã trạng thái (`StatusCode`)

| Mã | Giá trị | Mô tả |
|---|---|---|
| `STATUS_SUCCESS` | 0 | Thao tác thành công |
| `STATUS_ERR_FULL` | -1 | Server đã đầy, vượt quá `MAX_CLIENTS` |
| `STATUS_ERR_NICK_TAKEN` | -2 | Nickname đã có người sử dụng |
| `STATUS_ERR_INVALID_TARGET` | -3 | Không tìm thấy người nhận mục tiêu hoặc offline |
| `STATUS_ERR_NOT_REGISTERED` | -4 | Client chưa đăng ký nhưng đã gửi tin |
| `STATUS_ERR_INTERNAL` | -5 | Lỗi nội bộ hệ thống |

---

## 4. Cấu trúc gói tin

### A. Gói tin truyền trên FIFO (`chat_msg_t`)

Kích thước gói tin xấp xỉ 696 bytes (luôn nhỏ hơn 4096 bytes của `PIPE_BUF`).

```c
typedef struct {
    int msg_type;                       /* Loại thông điệp (MessageType) */
    int sender_id;                      /* ID của client gửi (-1 nếu chưa đăng ký) */
    pid_t sender_pid;                   /* PID của tiến trình gửi */
    char sender_nick[MAX_NICKNAME_LEN]; /* Nickname của client gửi */
    int target_id;                      /* ID người nhận (-1 nếu gửi broadcast) */
    char target_nick[MAX_NICKNAME_LEN]; /* Nickname người nhận (cho lệnh /msg) */
    char client_fifo[FIFO_PATH_LEN];    /* Đường dẫn FIFO phản hồi riêng của client */
    int status_code;                    /* Mã trạng thái (StatusCode) */
    size_t content_len;                 /* Độ dài thực tế nội dung */
    char content[MAX_MSG_LEN];          /* Nội dung tin nhắn / thông báo */
} chat_msg_t;
```

### B. Gói tin System V Message Queue (`worker_msg_t`)

Tuân thủ nghiêm ngặt tiêu chuẩn `msgsnd` và `msgrcv` trên UNIX/Linux:

```c
typedef struct {
    long mtype;                         /* Kênh phân loại (bắt buộc > 0) */
    chat_msg_t chat_msg;                /* Dữ liệu gói tin truyền từ Dispatcher sang Worker */
} worker_msg_t;
```

### C. Cấu trúc bộ nhớ chia sẻ (`shared_state_t`)

Ánh xạ trong System V Shared Memory:

```c
typedef struct {
    int client_id;
    pid_t pid;
    char nickname[MAX_NICKNAME_LEN];
    int is_online;
    char response_fifo[FIFO_PATH_LEN];
    time_t join_time;
} client_info_t;

typedef struct {
    int server_status;                  /* 1 = Server đang chạy, 0 = Đang dừng */
    pid_t server_pid;                   /* PID của Server Dispatcher */
    int active_client_count;            /* Số lượng client đang online thực tế */
    int next_client_id;                 /* Bộ đếm tự tăng cấp phát client_id */
    client_info_t clients[MAX_CLIENTS]; /* Bảng danh sách trạng thái người dùng */
} shared_state_t;
```

---

## 5. Quy trình trao đổi (Sequence Flow)

### 1. Đăng ký Client (`MSG_REGISTER`)
1. Client tạo `/tmp/ipc_chat_cli_<PID>.fifo`.
2. Client gửi gói `MSG_REGISTER` vào `/tmp/ipc_chat_srv.fifo`.
3. Server nhận từ FIFO, gửi `worker_msg_t` vào Message Queue.
4. Worker đọc Message Queue, khóa Semaphore `sem_lock`.
5. Worker kiểm tra trùng nickname và số lượng client trong Shared Memory.
6. Worker thêm client vào mảng `clients[]`, mở khóa Semaphore `sem_unlock`.
7. Worker gửi `MSG_REGISTER_RESP` về Client FIFO và broadcast `MSG_SERVER_NOTICE` tới toàn phòng.

### 2. Chat riêng tư (`/msg <target> <text>`)
1. Client phân tích cú pháp, đóng gói `MSG_CHAT_PRIVATE`.
2. Server chuyển vào Message Queue.
3. Worker khóa Semaphore, tra cứu `target_id` hoặc `target_nick` trong `clients[]`.
4. Mở FIFO riêng của người nhận và ghi gói tin trực tiếp.
5. Nếu không tìm thấy, gửi `MSG_ERROR` về cho người gửi.

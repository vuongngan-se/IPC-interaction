# Đặc Tả Giao Thức & Cấu Trúc Dữ Liệu (Protocol Specification)

Tài liệu này đặc tả chi tiết các loại thông điệp, cấu trúc dữ liệu, mã trạng thái và khuôn dạng gói tin trong hệ thống IPC Chat.

---

## 1. Giới hạn hệ thống

| Hằng số | Giá trị | Ý nghĩa |
|---|---|---|
| `MAX_CLIENTS` | 32 | Số lượng client tối đa kết nối đồng thời |
| `MAX_NICKNAME_LEN` | 32 | Độ dài tối đa của nickname (bao gồm `\0`) |
| `MAX_ROOM_NAME_LEN` | 32 | Độ dài tối đa tên phòng/kênh chat (vd: `#linux`, `#general`) |
| `DEFAULT_ROOM` | `"#general"` | Kênh mặc định khi tham gia chat |
| `MAX_MSG_LEN` | 512 | Độ dài tối đa nội dung văn bản tin nhắn |
| `FIFO_PATH_LEN` | 128 | Độ dài tối đa đường dẫn file FIFO trong `/tmp/` |
| `SERVER_FIFO_PATH` | `"/tmp/ipc_chat_srv.fifo"` | Đường dẫn FIFO tiếp nhận của Server |
| `CLIENT_FIFO_TEMPLATE` | `"/tmp/ipc_chat_cli_%d.fifo"` | Mẫu tên FIFO riêng theo PID của Client |
| `MIN_WORKERS` | 2 | Số Worker tối thiểu khởi tạo ban đầu |
| `MAX_WORKERS` | 8 | Giới hạn tối đa Worker Pool khi tự động co giãn |
| `SCALE_UP_THRESHOLD` | 3 | Ngưỡng tin nhắn dồn ứ trong Queue để kích hoạt thêm Worker |
| `PIPE_BUF` | 4096 bytes (Linux standard) | Ngưỡng ghi nguyên tử của Named Pipe POSIX |

---

## 2. Loại thông điệp (`MessageType`)

```c
typedef enum {
    MSG_REGISTER        = 1,    /* Client yêu cầu đăng ký phiên */
    MSG_REGISTER_RESP   = 2,    /* Server phản hồi kèm Client ID được cấp */
    MSG_CHAT_ALL        = 3,    /* Tin nhắn gửi phòng hiện tại (broadcast theo phòng) */
    MSG_CHAT_PRIVATE    = 4,    /* Tin nhắn riêng tư gửi một người (/msg) */
    MSG_LIST_USERS      = 5,    /* Yêu cầu xem danh sách người dùng online (/list) */
    MSG_LIST_RESP       = 6,    /* Phản hồi danh sách người dùng online */
    MSG_DISCONNECT      = 7,    /* Client thông báo ngắt kết nối (/quit) */
    MSG_SERVER_NOTICE   = 8,    /* Thông báo hệ thống (join/leave/shutdown) */
    MSG_ERROR           = 9,    /* Thông báo lỗi thao tác */
    MSG_ROOM_JOIN       = 10,   /* Yêu cầu chuyển/tham gia phòng (/join) */
    MSG_ROOM_LEAVE      = 11,   /* Rời phòng hiện tại về #general (/leave) */
    MSG_ROOM_LIST       = 12,   /* Xem danh sách phòng đang hoạt động (/rooms) */
    MSG_ROOM_RESP       = 13,   /* Phản hồi danh sách phòng hoặc xác nhận đổi phòng */
    MSG_FILE_OFFER      = 14,   /* Đề nghị gửi file Zero-Copy qua Shared Memory */
    MSG_FILE_ACCEPT     = 15,   /* Chấp nhận và xác nhận đã đọc xong file từ SHM */
    MSG_FILE_REJECT     = 16    /* Từ chối hoặc hủy nhận file */
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
| `STATUS_ERR_FILE_NOT_FOUND` | -6 | File không tồn tại hoặc không thể đọc |

---

## 4. Cấu trúc gói tin

### A. Gói tin truyền trên FIFO (`chat_msg_t`)

Kích thước gói tin xấp xỉ 820 bytes (luôn nhỏ hơn 4096 bytes của `PIPE_BUF`, đảm bảo nguyên tử).

```c
typedef struct {
    int msg_type;                       /* Loại thông điệp (MessageType) */
    int sender_id;                      /* ID của client gửi (-1 nếu chưa đăng ký) */
    pid_t sender_pid;                   /* PID của tiến trình gửi */
    char sender_nick[MAX_NICKNAME_LEN]; /* Nickname của client gửi */
    char room[MAX_ROOM_NAME_LEN];       /* Tên phòng/kênh chat */
    int target_id;                      /* ID người nhận (-1 nếu gửi broadcast) */
    char target_nick[MAX_NICKNAME_LEN]; /* Nickname người nhận (cho lệnh /msg) */
    char client_fifo[FIFO_PATH_LEN];    /* Đường dẫn FIFO phản hồi riêng của client */
    int status_code;                    /* Mã trạng thái (StatusCode) */
    char file_name[64];                 /* Tên file (cho truyền file zero-copy) */
    size_t file_size;                   /* Kích thước file theo bytes */
    int file_shmid;                     /* ID Shared Memory chứa nội dung file */
    key_t file_shm_key;                 /* Khóa Shared Memory */
    size_t content_len;                 /* Độ dài thực tế nội dung */
    char content[MAX_MSG_LEN];          /* Nội dung tin nhắn / thông báo */
} chat_msg_t;
```

### B. Gói tin System V Message Queue (`worker_msg_t`)

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
    char current_room[MAX_ROOM_NAME_LEN]; /* Phòng chat hiện tại của người dùng */
    int is_online;
    char response_fifo[FIFO_PATH_LEN];
    time_t join_time;
} client_info_t;

typedef struct {
    int server_status;                  /* 1 = Server đang chạy, 0 = Đang dừng */
    pid_t server_pid;                   /* PID của Server Dispatcher */
    int active_client_count;            /* Số lượng client đang online thực tế */
    int next_client_id;                 /* Bộ đếm tự tăng cấp phát client_id */
    int current_workers;                /* Số lượng worker hiện đang chạy */
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
6. Worker thêm client vào mảng `clients[]`, gán phòng mặc định `#general`, mở khóa Semaphore `sem_unlock`.
7. Worker gửi `MSG_REGISTER_RESP` về Client FIFO và broadcast `MSG_SERVER_NOTICE` tới người cùng phòng.

### 2. Chat phân luồng phòng (`/join <#room>`)
1. Client gửi `MSG_ROOM_JOIN` kèm tên phòng mong muốn.
2. Worker cập nhật `current_room` của client trong Shared Memory dưới bảo vệ của Semaphore.
3. Khi client gửi tin nhắn công cộng `MSG_CHAT_ALL`, Worker chỉ lặp qua những client có `current_room` trùng khớp để gửi bản tin, cô lập hoàn toàn với các phòng khác.

### 3. Truyền file Zero-Copy (`/sendfile <target> <path>`)
1. Người gửi kiểm tra file cục bộ, gọi `shmget(IPC_PRIVATE, file_size, ...)` tạo phân đoạn Shared Memory chuyên biệt và chép dữ liệu vào bằng `shmat()`.
2. Người gửi đóng gói `MSG_FILE_OFFER` (kèm `file_name`, `file_size`, `file_shmid`) gửi lên Server.
3. Worker điều phối chuyển `MSG_FILE_OFFER` tới FIFO của người nhận.
4. Người nhận gọi `shmat(file_shmid, NULL, SHM_RDONLY)` gắn trực tiếp vùng nhớ, ghi ra file `downloads/<file_name>`, sau đó tháo gỡ `shmdt()`.
5. Người nhận gửi phản hồi `MSG_FILE_ACCEPT` về cho người gửi.
6. Người gửi nhận thông báo thành công và gọi `shmctl(file_shmid, IPC_RMID)` giải phóng phân đoạn Shared Memory sạch sẽ, không tốn dung lượng trung gian trên đĩa hay qua kernel buffer.

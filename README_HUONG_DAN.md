# HƯỚNG DẪN CHẠY ENGLISH LEARNING APP

## Yêu cầu hệ thống
- Linux (Ubuntu) hoặc WSL (Windows Subsystem for Linux)
- G++ compiler với hỗ trợ C++17
- Không cần thư viện ngoài

## Cài đặt compiler (nếu chưa có)

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install g++ make

# Fedora
sudo dnf install g++ make
```

## Compile

### Cách 1: Dùng Makefile (khuyến nghị)

```bash
# Compile cả server và client
make all

# Hoặc compile riêng từng file
make server
make client
```

### Cách 2: Compile thủ công

```bash
# Compile server
g++ -std=c++17 -pthread -o server server.cpp

# Compile client
g++ -std=c++17 -pthread -o client client.cpp
```

## Chạy chương trình

### Bước 1: Khởi động Server

Mở terminal 1:
```bash
./server 8888
```

Hoặc dùng Makefile:
```bash
make run-server
```

Server sẽ hiển thị:
```
============================================
   ENGLISH LEARNING APP - SERVER
============================================
[INFO] Server started on port 8888
[INFO] Waiting for connections...
--------------------------------------------
Sample accounts:
  - student@example.com / student123
  - sarah@example.com / teacher123
--------------------------------------------
```

### Bước 2: Chạy Client

Mở terminal 2 (hoặc nhiều terminal cho nhiều client):
```bash
./client 127.0.0.1 8888
```

Hoặc dùng Makefile:
```bash
make run-client
```

## Sử dụng ứng dụng

### 1. Đăng nhập / Đăng ký

Khi khởi động client, bạn sẽ thấy menu:
```
============================================
       ENGLISH LEARNING APP
============================================
  1. Login
  2. Register
  0. Exit
--------------------------------------------
```

**Tài khoản mẫu:**
- Email: `student@example.com` | Password: `student123` (học sinh)
- Email: `sarah@example.com` | Password: `teacher123` (giáo viên)

Hoặc bạn có thể đăng ký tài khoản mới (chọn option 2).

### 2. Menu chính (sau khi đăng nhập)

```
============================================
     ENGLISH LEARNING APP - MAIN MENU
============================================
  User: Nguyen Van A | Level: beginner
--------------------------------------------
  1. Set English Level
  2. View Lessons
  3. Study a Lesson
  4. Take a Test
  5. Chat with Others
  6. Logout
  0. Exit
--------------------------------------------
```

### 3. Các chức năng chi tiết

#### a) Set English Level (Option 1)
Chọn mức độ học:
- `beginner` - Người mới bắt đầu
- `intermediate` - Trung cấp
- `advanced` - Nâng cao

#### b) View Lessons (Option 2)
Xem danh sách bài học theo mức độ của bạn. Có thể lọc theo topic:
- grammar
- vocabulary
- listening
- speaking
- reading
- writing

#### c) Study a Lesson (Option 3)
Nhập Lesson ID để học bài (ví dụ: `lesson_001`)

Các bài học mẫu có sẵn:
- `lesson_001` - Present Simple Tense (Grammar, Beginner)
- `lesson_002` - Common Daily Vocabulary (Vocabulary, Beginner)
- `lesson_003` - Past Tenses (Grammar, Intermediate)

#### d) Take a Test (Option 4)
Làm bài kiểm tra trắc nghiệm và điền từ.
- Trả lời câu hỏi multiple choice bằng a/b/c/d
- Điền từ vào chỗ trống
- Xem kết quả chi tiết sau khi nộp bài

#### e) Chat with Others (Option 5)
- Xem danh sách người dùng online/offline
- Chọn người để chat
- Gửi tin nhắn real-time
- Gõ `exit` để thoát chat

## Chạy nhiều Client (Chat test)

Để test chức năng chat:

1. Mở Terminal 1: `./server 8888`
2. Mở Terminal 2: `./client 127.0.0.1 8888` → Đăng nhập với `student@example.com`
3. Mở Terminal 3: `./client 127.0.0.1 8888` → Đăng nhập với `sarah@example.com`
4. Từ client 1, vào Chat → chọn client 2 → gửi tin nhắn
5. Client 2 sẽ nhận được notification real-time

## Cấu trúc giao thức

Tất cả messages được gửi/nhận theo format:
```
[4 bytes: message length][JSON message]
```

Message types được implement:
- `REGISTER_REQUEST/RESPONSE` - Đăng ký
- `LOGIN_REQUEST/RESPONSE` - Đăng nhập
- `SET_LEVEL_REQUEST/RESPONSE` - Chọn level
- `GET_LESSONS_REQUEST/RESPONSE` - Lấy danh sách bài học
- `GET_LESSON_DETAIL_REQUEST/RESPONSE` - Lấy chi tiết bài học
- `GET_TEST_REQUEST/RESPONSE` - Lấy đề test
- `SUBMIT_TEST_REQUEST/RESPONSE` - Nộp bài test
- `GET_CONTACT_LIST_REQUEST/RESPONSE` - Lấy danh sách contacts
- `SEND_MESSAGE_REQUEST/RESPONSE` - Gửi tin nhắn
- `RECEIVE_MESSAGE` - Nhận tin nhắn (push từ server)

## Log files

Server ghi log vào file `server.log` với format:
```
[Timestamp] RECV/SEND ClientIP:Port: message_content
```

## Troubleshooting

### Lỗi "Address already in use"
```bash
# Tìm process đang dùng port 8888
lsof -i :8888

# Kill process
kill -9 <PID>
```

### Lỗi "Connection refused"
- Đảm bảo server đang chạy
- Kiểm tra đúng IP và port

### Không compile được
```bash
# Kiểm tra g++ version
g++ --version

# Cần g++ >= 7.0 cho C++17
```

## Dọn dẹp

```bash
make clean
```

## Mở rộng

Các tính năng có thể thêm:
- Voice call (cần thêm WebRTC hoặc UDP streaming)
- Lưu trữ dữ liệu vào database (SQLite, MySQL)
- Mã hóa password (bcrypt)
- Session token JWT thực sự
- HTTPS/TLS cho bảo mật

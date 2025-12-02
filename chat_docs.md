# Tài liệu chi tiết: Chức năng Chat Real-time

## Mục lục
1. [Tổng quan chức năng Chat](#1-tổng-quan-chức-năng-chat)
2. [Cấu trúc dữ liệu](#2-cấu-trúc-dữ-liệu)
3. [Kiến trúc hệ thống Chat](#3-kiến-trúc-hệ-thống-chat)
4. [Luồng xử lý chi tiết](#4-luồng-xử-lý-chi-tiết)
5. [Xử lý phía Server](#5-xử-lý-phía-server)
6. [Xử lý phía Client](#6-xử-lý-phía-client)
7. [Các trường hợp sử dụng](#7-các-trường-hợp-sử-dụng)
8. [Đồng bộ hóa và Thread Safety](#8-đồng-bộ-hóa-và-thread-safety)

---

## 1. Tổng quan chức năng Chat

### 1.1 Mô tả
Chức năng chat cho phép các user trong hệ thống gửi tin nhắn cho nhau theo thời gian thực (real-time). Hệ thống hỗ trợ:

- **Chat 1-1**: Gửi tin nhắn trực tiếp giữa 2 user
- **Real-time delivery**: Tin nhắn được gửi ngay lập tức khi recipient online
- **Offline message**: Lưu tin nhắn khi recipient offline, thông báo khi họ online lại
- **Chat history**: Lưu trữ và hiển thị lịch sử chat
- **Read status**: Đánh dấu tin nhắn đã đọc/chưa đọc

### 1.2 Các thành phần chính

```
┌─────────────────────────────────────────────────────────────────┐
│                     HỆ THỐNG CHAT                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────┐      ┌─────────────┐      ┌─────────────┐     │
│  │   CLIENT    │      │   SERVER    │      │   CLIENT    │     │
│  │      A      │◄────►│             │◄────►│      B      │     │
│  └─────────────┘      └─────────────┘      └─────────────┘     │
│        │                    │                    │              │
│        │              ┌─────┴─────┐              │              │
│        │              │  Storage  │              │              │
│        │              │           │              │              │
│        │              │ - Users   │              │              │
│        │              │ - Messages│              │              │
│        │              │ - Sessions│              │              │
│        │              └───────────┘              │              │
│        │                                         │              │
│  ┌─────┴─────┐                             ┌─────┴─────┐       │
│  │ Main      │                             │ Main      │       │
│  │ Thread    │                             │ Thread    │       │
│  ├───────────┤                             ├───────────┤       │
│  │ Receive   │                             │ Receive   │       │
│  │ Thread    │                             │ Thread    │       │
│  └───────────┘                             └───────────┘       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. Cấu trúc dữ liệu

### 2.1 ChatMessage (Server)

```cpp
struct ChatMessage {
    std::string messageId;    // ID duy nhất: "chatmsg_1", "chatmsg_2"...
    std::string senderId;     // ID người gửi: "user_1"
    std::string recipientId;  // ID người nhận: "user_2"
    std::string content;      // Nội dung tin nhắn
    long long timestamp;      // Thời điểm gửi (milliseconds từ epoch)
    bool read;                // Trạng thái: true = đã đọc, false = chưa đọc
};
```

### 2.2 User (Server) - Các trường liên quan đến chat

```cpp
struct User {
    std::string userId;       // ID user
    std::string fullname;     // Tên hiển thị trong chat
    bool online;              // Trạng thái online/offline
    int clientSocket;         // Socket descriptor để gửi push notification
    // ... các trường khác
};
```

### 2.3 Biến toàn cục phía Client

```cpp
// Trạng thái chat
std::atomic<bool> inChatMode(false);      // Đang trong màn hình chat?
std::string currentChatPartnerId = "";     // ID người đang chat cùng
std::string currentChatPartnerName = "";   // Tên người đang chat cùng
std::mutex chatPartnerMutex;               // Mutex bảo vệ biến trên

// Thông báo tin nhắn mới
std::atomic<bool> hasNewNotification(false);  // Có tin nhắn mới?
std::string pendingChatUserId = "";           // ID người gửi tin nhắn mới
std::string pendingChatUserName = "";         // Tên người gửi
std::mutex notificationMutex;                 // Mutex bảo vệ
```

### 2.4 Message Types liên quan đến Chat

| Message Type | Hướng | Mô tả |
|--------------|-------|-------|
| `GET_CONTACT_LIST_REQUEST` | Client → Server | Lấy danh sách contacts |
| `GET_CONTACT_LIST_RESPONSE` | Server → Client | Trả về danh sách contacts |
| `SEND_MESSAGE_REQUEST` | Client → Server | Gửi tin nhắn |
| `SEND_MESSAGE_RESPONSE` | Server → Client | Xác nhận đã gửi |
| `RECEIVE_MESSAGE` | Server → Client | Push notification tin nhắn mới |
| `GET_CHAT_HISTORY_REQUEST` | Client → Server | Lấy lịch sử chat |
| `GET_CHAT_HISTORY_RESPONSE` | Server → Client | Trả về lịch sử chat |
| `MARK_MESSAGES_READ_REQUEST` | Client → Server | Đánh dấu đã đọc |
| `MARK_MESSAGES_READ_RESPONSE` | Server → Client | Xác nhận đã đánh dấu |
| `UNREAD_MESSAGES_NOTIFICATION` | Server → Client | Thông báo tin nhắn chưa đọc khi login |

---

## 3. Kiến trúc hệ thống Chat

### 3.1 Mô hình Request-Response + Push Notification

```
┌──────────────────────────────────────────────────────────────────────┐
│                         CHAT ARCHITECTURE                             │
│                                                                       │
│   CLIENT A                    SERVER                    CLIENT B      │
│  ┌────────┐                 ┌────────┐                 ┌────────┐    │
│  │        │                 │        │                 │        │    │
│  │  Main  │ ──Request────►  │ Client │                 │  Main  │    │
│  │ Thread │ ◄──Response──── │ Thread │                 │ Thread │    │
│  │        │                 │   A    │                 │        │    │
│  ├────────┤                 ├────────┤                 ├────────┤    │
│  │Receive │                 │ Client │ ───Push──────►  │Receive │    │
│  │ Thread │                 │ Thread │   Notification  │ Thread │    │
│  │        │                 │   B    │                 │        │    │
│  └────────┘                 └────────┘                 └────────┘    │
│                                                                       │
│  Request-Response: Đồng bộ, client chờ response                      │
│  Push Notification: Bất đồng bộ, server chủ động gửi                 │
└──────────────────────────────────────────────────────────────────────┘
```

### 3.2 Luồng dữ liệu tổng quan

```
┌─────────┐                                              ┌─────────┐
│CLIENT A │                                              │CLIENT B │
└────┬────┘                                              └────┬────┘
     │                                                        │
     │  1. SEND_MESSAGE_REQUEST                               │
     │  ┌─────────────────────────────┐                       │
     │  │ messageType: "SEND_MESSAGE" │                       │
     │  │ recipientId: "user_B"       │                       │
     │  │ messageContent: "Hello!"    │                       │
     │  └─────────────────────────────┘                       │
     │─────────────────────┐                                  │
     │                     ▼                                  │
     │              ┌─────────────┐                           │
     │              │   SERVER    │                           │
     │              │             │                           │
     │              │ 2. Lưu msg  │                           │
     │              │ 3. Check B  │                           │
     │              │    online?  │                           │
     │              └──────┬──────┘                           │
     │                     │                                  │
     │                     │ YES, B online                    │
     │                     │                                  │
     │  4. SEND_MESSAGE_   │  5. RECEIVE_MESSAGE              │
     │     RESPONSE        │  ┌─────────────────────────────┐ │
     │  ┌────────────────┐ │  │ messageType: "RECEIVE_MSG"  │ │
     │  │ delivered:true │ │  │ senderId: "user_A"          │ │
     │  └────────────────┘ │  │ senderName: "Alice"         │ │
     │◄────────────────────┤  │ messageContent: "Hello!"    │ │
     │                     │  └─────────────────────────────┘ │
     │                     │─────────────────────────────────►│
     │                     │                                  │
     │                     │                    6. Hiển thị   │
     │                     │                       tin nhắn   │
     │                     │                                  │
```

---

## 4. Luồng xử lý chi tiết

### 4.1 Luồng gửi tin nhắn (Send Message Flow)

```
┌─────────────────────────────────────────────────────────────────────┐
│                      SEND MESSAGE FLOW                               │
└─────────────────────────────────────────────────────────────────────┘

CLIENT (Sender)                 SERVER                    CLIENT (Recipient)
      │                           │                              │
      │ User nhập tin nhắn        │                              │
      │ và nhấn Enter             │                              │
      ▼                           │                              │
┌─────────────┐                   │                              │
│ trim(msg)   │ Loại bỏ           │                              │
│             │ khoảng trắng      │                              │
└──────┬──────┘                   │                              │
       │                          │                              │
       ▼                          │                              │
┌─────────────┐                   │                              │
│ Build JSON  │                   │                              │
│ Request     │                   │                              │
└──────┬──────┘                   │                              │
       │                          │                              │
       │ SEND_MESSAGE_REQUEST     │                              │
       │─────────────────────────►│                              │
       │                          │                              │
       │                    ┌─────┴─────┐                        │
       │                    │ Validate  │                        │
       │                    │ Session   │                        │
       │                    └─────┬─────┘                        │
       │                          │                              │
       │                    ┌─────┴─────┐                        │
       │                    │ Create    │                        │
       │                    │ Message   │                        │
       │                    │ Object    │                        │
       │                    └─────┬─────┘                        │
       │                          │                              │
       │                    ┌─────┴─────┐                        │
       │                    │ Store in  │                        │
       │                    │ chatMsgs  │                        │
       │                    │ vector    │                        │
       │                    └─────┬─────┘                        │
       │                          │                              │
       │                    ┌─────┴─────┐                        │
       │                    │ Check     │                        │
       │                    │ Recipient │                        │
       │                    │ Online?   │                        │
       │                    └─────┬─────┘                        │
       │                          │                              │
       │                          │ IF ONLINE                    │
       │                          │─────────────────────────────►│
       │                          │ RECEIVE_MESSAGE              │
       │                          │                              │
       │                          │                        ┌─────┴─────┐
       │                          │                        │ Receive   │
       │                          │                        │ Thread    │
       │                          │                        │ nhận msg  │
       │                          │                        └─────┬─────┘
       │                          │                              │
       │                          │                        ┌─────┴─────┐
       │                          │                        │ Check     │
       │                          │                        │ đang chat │
       │                          │                        │ với sender│
       │                          │                        └─────┬─────┘
       │                          │                              │
       │                          │                              │ IF YES
       │                          │                        ┌─────┴─────┐
       │                          │                        │ Hiển thị  │
       │                          │                        │ trực tiếp │
       │                          │                        │ trong chat│
       │                          │                        └───────────┘
       │                          │                              │
       │                          │                              │ IF NO
       │                          │                        ┌─────┴─────┐
       │                          │                        │ Hiển thị  │
       │                          │                        │ popup     │
       │                          │                        │ thông báo │
       │                          │                        └───────────┘
       │                          │                              │
       │ SEND_MESSAGE_RESPONSE    │                              │
       │◄─────────────────────────│                              │
       │ {delivered: true/false}  │                              │
       │                          │                              │
 ┌─────┴─────┐                    │                              │
 │ Hiển thị  │                    │                              │
 │ trạng thái│                    │                              │
 │ gửi       │                    │                              │
 └───────────┘                    │                              │
```

### 4.2 Luồng nhận tin nhắn khi offline

```
┌─────────────────────────────────────────────────────────────────────┐
│                    OFFLINE MESSAGE FLOW                              │
└─────────────────────────────────────────────────────────────────────┘

Timeline:
─────────────────────────────────────────────────────────────────────►

T1: User B offline          T2: A gửi msg           T3: B login lại
        │                         │                        │
        ▼                         ▼                        ▼

CLIENT A                    SERVER                    CLIENT B
   │                          │                          │
   │                          │                     (OFFLINE)
   │                          │                          X
   │ SEND_MESSAGE_REQUEST     │                          │
   │─────────────────────────►│                          │
   │                          │                          │
   │                    ┌─────┴─────┐                    │
   │                    │ Store msg │                    │
   │                    │ read=false│                    │
   │                    └─────┬─────┘                    │
   │                          │                          │
   │                    ┌─────┴─────┐                    │
   │                    │ B online? │                    │
   │                    │    NO     │                    │
   │                    └─────┬─────┘                    │
   │                          │                          │
   │ SEND_MESSAGE_RESPONSE    │                          │
   │◄─────────────────────────│                          │
   │ {delivered: false}       │                          │
   │                          │                          │
   │ [Sent - User offline]    │                          │
   │                          │                          │
   │                          │        ... Thời gian trôi qua ...
   │                          │                          │
   │                          │                    (B ONLINE)
   │                          │                          │
   │                          │     LOGIN_REQUEST        │
   │                          │◄─────────────────────────│
   │                          │                          │
   │                    ┌─────┴─────┐                    │
   │                    │ Validate  │                    │
   │                    │ Login     │                    │
   │                    └─────┬─────┘                    │
   │                          │                          │
   │                          │     LOGIN_RESPONSE       │
   │                          │─────────────────────────►│
   │                          │                          │
   │                    ┌─────┴─────┐                    │
   │                    │ Query     │                    │
   │                    │ unread    │                    │
   │                    │ messages  │                    │
   │                    └─────┬─────┘                    │
   │                          │                          │
   │                          │ UNREAD_MESSAGES_NOTIF    │
   │                          │─────────────────────────►│
   │                          │ {unreadCount: 1,         │
   │                          │  messages: [...]}        │
   │                          │                          │
   │                          │                    ┌─────┴─────┐
   │                          │                    │ Hiển thị  │
   │                          │                    │ thông báo │
   │                          │                    │ tin nhắn  │
   │                          │                    │ chưa đọc  │
   │                          │                    └───────────┘
```

---

## 5. Xử lý phía Server

### 5.1 handleSendMessage - Xử lý gửi tin nhắn

```cpp
std::string handleSendMessage(const std::string& json, int clientSocket) {
    // ========== BƯỚC 1: Parse request ==========
    std::string payload = getJsonObject(json, "payload");
    std::string messageId = getJsonValue(json, "messageId");
    std::string sessionToken = getJsonValue(json, "sessionToken");
    std::string recipientId = getJsonValue(payload, "recipientId");
    std::string messageContent = getJsonValue(payload, "messageContent");

    // ========== BƯỚC 2: Validate session ==========
    std::string senderId = validateSession(sessionToken);
    if (senderId.empty()) {
        return errorResponse("Invalid or expired session");
    }

    // ========== BƯỚC 3: Tạo ChatMessage object ==========
    ChatMessage msg;
    msg.messageId = generateId("chatmsg");  // "chatmsg_1", "chatmsg_2"...
    msg.senderId = senderId;
    msg.recipientId = recipientId;
    msg.content = messageContent;
    msg.timestamp = getCurrentTimestamp();
    msg.read = false;  // Mặc định chưa đọc

    // ========== BƯỚC 4: Lưu vào storage (thread-safe) ==========
    {
        std::lock_guard<std::mutex> lock(chatMutex);
        chatMessages.push_back(msg);
    }

    // ========== BƯỚC 5: Tìm recipient và check online ==========
    bool delivered = false;
    User* recipient = nullptr;

    {
        std::lock_guard<std::mutex> lock(usersMutex);
        auto it = userById.find(recipientId);
        if (it != userById.end()) {
            recipient = it->second;
        }
    }

    // ========== BƯỚC 6: Gửi push notification nếu online ==========
    if (recipient && recipient->online && recipient->clientSocket > 0) {
        // Lấy tên người gửi
        std::string senderName = "Unknown";
        {
            std::lock_guard<std::mutex> lock(usersMutex);
            auto senderIt = userById.find(senderId);
            if (senderIt != userById.end()) {
                senderName = senderIt->second->fullname;
            }
        }

        // Build push notification JSON
        std::string notification = R"({
            "messageType": "RECEIVE_MESSAGE",
            "messageId": ")" + msg.messageId + R"(",
            "timestamp": )" + std::to_string(getCurrentTimestamp()) + R"(,
            "payload": {
                "messageId": ")" + msg.messageId + R"(",
                "senderId": ")" + senderId + R"(",
                "senderName": ")" + senderName + R"(",
                "messageContent": ")" + messageContent + R"(",
                "sentAt": )" + std::to_string(msg.timestamp) + R"(
            }
        })";

        // Gửi đến recipient socket
        uint32_t len = htonl(notification.length());
        if (send(recipient->clientSocket, &len, sizeof(len), 0) > 0) {
            if (send(recipient->clientSocket, notification.c_str(),
                     notification.length(), 0) > 0) {
                delivered = true;
            }
        }
    }

    // ========== BƯỚC 7: Trả về response ==========
    return R"({
        "messageType": "SEND_MESSAGE_RESPONSE",
        "messageId": ")" + messageId + R"(",
        "payload": {
            "status": "success",
            "data": {
                "messageId": ")" + msg.messageId + R"(",
                "sentAt": )" + std::to_string(msg.timestamp) + R"(,
                "delivered": )" + (delivered ? "true" : "false") + R"(
            }
        }
    })";
}
```

### 5.2 sendUnreadMessagesNotification - Gửi tin nhắn chưa đọc khi login

```cpp
void sendUnreadMessagesNotification(int clientSocket, const std::string& userId) {
    // ========== BƯỚC 1: Query tin nhắn chưa đọc ==========
    std::vector<ChatMessage> unreadMessages;
    {
        std::lock_guard<std::mutex> lock(chatMutex);
        for (auto& msg : chatMessages) {
            // Tìm tin nhắn gửi cho user này và chưa đọc
            if (msg.recipientId == userId && !msg.read) {
                unreadMessages.push_back(msg);
            }
        }
    }

    // Không có tin nhắn chưa đọc -> không gửi gì
    if (unreadMessages.empty()) return;

    // ========== BƯỚC 2: Build danh sách tin nhắn với tên người gửi ==========
    std::stringstream messagesJson;
    messagesJson << "[";
    bool first = true;

    for (const auto& msg : unreadMessages) {
        // Lấy tên người gửi
        std::string senderName = "Unknown";
        {
            std::lock_guard<std::mutex> lock(usersMutex);
            auto it = userById.find(msg.senderId);
            if (it != userById.end()) {
                senderName = it->second->fullname;
            }
        }

        if (!first) messagesJson << ",";
        first = false;

        messagesJson << R"({
            "messageId": ")" << msg.messageId << R"(",
            "senderId": ")" << msg.senderId << R"(",
            "senderName": ")" << senderName << R"(",
            "content": ")" << msg.content << R"(",
            "timestamp": )" << msg.timestamp << R"(
        })";
    }
    messagesJson << "]";

    // ========== BƯỚC 3: Build và gửi notification ==========
    std::string notification = R"({
        "messageType": "UNREAD_MESSAGES_NOTIFICATION",
        "messageId": ")" + generateId("notif") + R"(",
        "timestamp": )" + std::to_string(getCurrentTimestamp()) + R"(,
        "payload": {
            "unreadCount": )" + std::to_string(unreadMessages.size()) + R"(,
            "messages": )" + messagesJson.str() + R"(
        }
    })";

    uint32_t len = htonl(notification.length());
    send(clientSocket, &len, sizeof(len), 0);
    send(clientSocket, notification.c_str(), notification.length(), 0);
}
```

### 5.3 handleGetChatHistory - Lấy lịch sử chat

```cpp
std::string handleGetChatHistory(const std::string& json) {
    // Parse request
    std::string recipientId = getJsonValue(payload, "recipientId");
    std::string userId = validateSession(sessionToken);

    // Query messages giữa 2 user
    std::vector<ChatMessage> history;
    {
        std::lock_guard<std::mutex> lock(chatMutex);
        for (const auto& msg : chatMessages) {
            // Lấy tin nhắn 2 chiều giữa userId và recipientId
            bool isRelevant =
                (msg.senderId == userId && msg.recipientId == recipientId) ||
                (msg.senderId == recipientId && msg.recipientId == userId);

            if (isRelevant) {
                history.push_back(msg);
            }
        }
    }

    // Sort theo timestamp
    std::sort(history.begin(), history.end(),
        [](const ChatMessage& a, const ChatMessage& b) {
            return a.timestamp < b.timestamp;
        });

    // Build response JSON
    // ...
}
```

### 5.4 handleMarkMessagesRead - Đánh dấu đã đọc

```cpp
std::string handleMarkMessagesRead(const std::string& json) {
    std::string senderId = getJsonValue(payload, "senderId");  // Người gửi tin nhắn
    std::string userId = validateSession(sessionToken);         // Người nhận (current user)

    int markedCount = 0;
    {
        std::lock_guard<std::mutex> lock(chatMutex);
        for (auto& msg : chatMessages) {
            // Đánh dấu tin nhắn từ senderId gửi cho userId
            if (msg.recipientId == userId &&
                msg.senderId == senderId &&
                !msg.read) {
                msg.read = true;
                markedCount++;
            }
        }
    }

    return successResponse(markedCount);
}
```

---

## 6. Xử lý phía Client

### 6.1 Background Receive Thread

```cpp
void receiveThreadFunc() {
    while (running && clientSocket >= 0) {
        // ========== BƯỚC 1: Non-blocking check với poll() ==========
        struct pollfd pfd;
        pfd.fd = clientSocket;
        pfd.events = POLLIN;  // Chờ có dữ liệu đọc

        int ret = poll(&pfd, 1, 100);  // Timeout 100ms

        if (ret <= 0) continue;  // Timeout hoặc error

        // ========== BƯỚC 2: Đọc message từ server ==========
        if (pfd.revents & POLLIN) {
            // Đọc độ dài message (4 bytes)
            uint32_t msgLen;
            recv(clientSocket, &msgLen, sizeof(msgLen), MSG_WAITALL);
            msgLen = ntohl(msgLen);

            // Đọc nội dung message
            std::string buffer(msgLen, '\0');
            recv(clientSocket, &buffer[0], msgLen, 0);

            // ========== BƯỚC 3: Phân loại message ==========
            std::string messageType = getJsonValue(buffer, "messageType");

            if (messageType == "RECEIVE_MESSAGE" ||
                messageType == "UNREAD_MESSAGES_NOTIFICATION") {
                // Push notification -> Xử lý ngay
                handlePushNotification(buffer);
            } else {
                // Response cho request -> Đưa vào queue
                {
                    std::lock_guard<std::mutex> lock(responseQueueMutex);
                    responseQueue.push(buffer);
                }
                responseCondition.notify_one();  // Báo main thread
            }
        }
    }
}
```

**Giải thích:**
- Thread này chạy liên tục trong background
- Sử dụng `poll()` với timeout 100ms để không block CPU
- Phân loại message:
  - **Push notification**: Xử lý ngay bằng `handlePushNotification()`
  - **Response**: Đưa vào queue để main thread lấy

### 6.2 handlePushNotification - Xử lý tin nhắn đến

```cpp
void handlePushNotification(const std::string& message) {
    std::string messageType = getJsonValue(message, "messageType");

    if (messageType == "RECEIVE_MESSAGE") {
        // ========== Parse thông tin tin nhắn ==========
        std::string payload = getJsonObject(message, "payload");
        std::string senderId = getJsonValue(payload, "senderId");
        std::string senderName = getJsonValue(payload, "senderName");
        std::string messageContent = getJsonValue(payload, "messageContent");

        // ========== Kiểm tra đang chat với người này không ==========
        bool isChattingWithSender = false;
        {
            std::lock_guard<std::mutex> lock(chatPartnerMutex);
            isChattingWithSender = (inChatMode && currentChatPartnerId == senderId);
        }

        if (isChattingWithSender) {
            // ===== TRƯỜNG HỢP 1: Đang trong cửa sổ chat với người gửi =====
            // Hiển thị tin nhắn trực tiếp trong chat
            std::lock_guard<std::mutex> lock(printMutex);
            std::cout << "\n" << senderName << ": " << messageContent << "\n";
            std::cout << "You: " << std::flush;  // In lại prompt
        }
        else {
            // ===== TRƯỜNG HỢP 2: Không đang chat với người gửi =====
            // Lưu thông tin để quick reply
            {
                std::lock_guard<std::mutex> lock(notificationMutex);
                pendingChatUserId = senderId;
                pendingChatUserName = senderName;
                hasNewNotification = true;
            }

            // Hiển thị popup thông báo
            if (canShowNotification) {
                std::lock_guard<std::mutex> lock(printMutex);
                std::cout << "\n";
                std::cout << "╔══════════════════════════════════════════╗\n";
                std::cout << "║  📬 NEW MESSAGE from " << senderName << "\n";
                std::cout << "║  \"" << messageContent << "\"\n";
                std::cout << "║  Press 'r' in menu to reply quickly      ║\n";
                std::cout << "╚══════════════════════════════════════════╝\n";
            }
        }
    }
    else if (messageType == "UNREAD_MESSAGES_NOTIFICATION") {
        // Xử lý thông báo tin nhắn chưa đọc khi login
        // ... (hiển thị danh sách tin nhắn chưa đọc)
    }
}
```

**Sơ đồ quyết định:**
```
                    ┌─────────────────────┐
                    │ Nhận RECEIVE_MESSAGE│
                    └──────────┬──────────┘
                               │
                               ▼
                    ┌─────────────────────┐
                    │ inChatMode == true  │
                    │        AND          │
                    │ currentChatPartnerId│
                    │    == senderId?     │
                    └──────────┬──────────┘
                               │
              ┌────────────────┴────────────────┐
              │ YES                             │ NO
              ▼                                 ▼
    ┌─────────────────────┐          ┌─────────────────────┐
    │ Hiển thị tin nhắn   │          │ Lưu pending info    │
    │ trực tiếp trong     │          │ hasNewNotification  │
    │ cửa sổ chat         │          │ = true              │
    │                     │          │                     │
    │ "Alice: Hello!"     │          │ Hiển thị popup      │
    │ "You: _"            │          │ thông báo           │
    └─────────────────────┘          └─────────────────────┘
```

### 6.3 openChatWith - Mở cửa sổ chat

```cpp
void openChatWith(const std::string& recipientId, const std::string& recipientName) {
    clearScreen();
    printHeader("Chatting with: " + recipientName);

    // ========== BƯỚC 1: Lấy lịch sử chat ==========
    std::string historyRequest = buildGetChatHistoryRequest(recipientId);
    std::string historyResponse = sendAndReceive(historyRequest);

    // Hiển thị lịch sử chat
    displayChatHistory(historyResponse, recipientName);

    // ========== BƯỚC 2: Đánh dấu tin nhắn đã đọc ==========
    std::string markReadRequest = buildMarkReadRequest(recipientId);
    sendAndReceive(markReadRequest);

    // ========== BƯỚC 3: Clear pending notification nếu đang chat với người này ==========
    {
        std::lock_guard<std::mutex> lock(notificationMutex);
        if (pendingChatUserId == recipientId) {
            hasNewNotification = false;
            pendingChatUserId = "";
            pendingChatUserName = "";
        }
    }

    // ========== BƯỚC 4: Set current chat partner ==========
    // Quan trọng: Để receive thread biết đang chat với ai
    {
        std::lock_guard<std::mutex> lock(chatPartnerMutex);
        currentChatPartnerId = recipientId;
        currentChatPartnerName = recipientName;
    }

    inChatMode = true;

    // ========== BƯỚC 5: Chat loop ==========
    while (inChatMode && running) {
        printColored("You: ", "green");

        std::string message;
        std::getline(std::cin, message);

        // Loại bỏ khoảng trắng đầu/cuối
        trim(message);

        if (message == "exit") {
            inChatMode = false;
            break;
        }
        if (message.empty()) continue;

        // Gửi tin nhắn
        std::string chatRequest = buildSendMessageRequest(recipientId, message);
        std::string chatResponse = sendAndReceive(chatRequest);

        // Hiển thị trạng thái
        std::string delivered = getJsonValue(chatResponse, "delivered");
        if (delivered == "true") {
            printColored("[Delivered ✓]\n", "green");
        } else {
            printColored("[Sent - User offline]\n", "yellow");
        }
    }

    // ========== BƯỚC 6: Cleanup khi thoát chat ==========
    {
        std::lock_guard<std::mutex> lock(chatPartnerMutex);
        currentChatPartnerId = "";
        currentChatPartnerName = "";
    }
    inChatMode = false;
}
```

### 6.4 Hàm trim - Loại bỏ khoảng trắng

```cpp
void trim(std::string &s) {
    // Xóa ký tự trắng ở đầu string
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
        [](unsigned char ch) { return !std::isspace(ch); }
    ));

    // Xóa ký tự trắng ở cuối string
    s.erase(std::find_if(s.rbegin(), s.rend(),
        [](unsigned char ch) { return !std::isspace(ch); }
    ).base(), s.end());
}
```

**Tại sao cần trim?**
```
Input: "  exit  "     →  trim()  →  "exit"     ✓ Thoát được
Input: "  hello  "    →  trim()  →  "hello"    ✓ Gửi tin nhắn đúng
Input: "   "          →  trim()  →  ""         ✓ Bỏ qua (empty)
```

---

## 7. Các trường hợp sử dụng

### 7.1 Trường hợp 1: Cả 2 user đang online và chat với nhau

```
┌─────────────────────────────────────────────────────────────────────┐
│ CLIENT A (Chat with B)              CLIENT B (Chat with A)          │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│ ╔═══════════════════════════╗    ╔═══════════════════════════╗     │
│ ║ Chatting with: Bob        ║    ║ Chatting with: Alice      ║     │
│ ╠═══════════════════════════╣    ╠═══════════════════════════╣     │
│ ║ --- Chat History ---      ║    ║ --- Chat History ---      ║     │
│ ║ You: Hi Bob!              ║    ║ Alice: Hi Bob!            ║     │
│ ║ Bob: Hello Alice!         ║    ║ You: Hello Alice!         ║     │
│ ║ --- End of History ---    ║    ║ --- End of History ---    ║     │
│ ║                           ║    ║                           ║     │
│ ║ You: How are you?         ║    ║                           ║     │
│ ║ [Delivered ✓]             ║    ║ Alice: How are you?       ║ ◄───┼── Hiển thị ngay
│ ║                           ║    ║ You: I'm fine, thanks!    ║     │
│ ║ Bob: I'm fine, thanks!    ║ ◄──╫───                        ║     │
│ ║ You: _                    ║    ║ [Delivered ✓]             ║     │
│ ╚═══════════════════════════╝    ║ You: _                    ║     │
│                                  ╚═══════════════════════════╝     │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### 7.2 Trường hợp 2: User B online nhưng ở main menu

```
┌─────────────────────────────────────────────────────────────────────┐
│ CLIENT A (Chat with B)              CLIENT B (Main Menu)            │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│ ╔═══════════════════════════╗    ╔═══════════════════════════╗     │
│ ║ Chatting with: Bob        ║    ║ ENGLISH LEARNING APP      ║     │
│ ╠═══════════════════════════╣    ╠═══════════════════════════╣     │
│ ║                           ║    ║ 1. Set English Level      ║     │
│ ║ You: Hello Bob!           ║    ║ 2. View All Lessons       ║     │
│ ║ [Delivered ✓]             ║    ║ 3. Take a Test            ║     │
│ ║ You: _                    ║    ║ 4. Chat with Others       ║     │
│ ╚═══════════════════════════╝    ║ 5. Logout                 ║     │
│                                  ╠═══════════════════════════╣     │
│                                  ║                           ║     │
│                                  ║ ╔═════════════════════╗   ║     │
│                                  ║ ║ 📬 NEW MESSAGE from ║   ║ ◄───┼── Popup xuất hiện
│                                  ║ ║ Alice               ║   ║     │
│                                  ║ ║ "Hello Bob!"        ║   ║     │
│                                  ║ ║ Press 'r' to reply  ║   ║     │
│                                  ║ ╚═════════════════════╝   ║     │
│                                  ║                           ║     │
│                                  ║ Enter your choice: _      ║     │
│                                  ╚═══════════════════════════╝     │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### 7.3 Trường hợp 3: User B offline

```
┌─────────────────────────────────────────────────────────────────────┐
│ CLIENT A (Chat with B)              CLIENT B (OFFLINE)              │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│ ╔═══════════════════════════╗                                       │
│ ║ Chatting with: Bob        ║           ┌─────────────────┐         │
│ ╠═══════════════════════════╣           │                 │         │
│ ║                           ║           │    OFFLINE      │         │
│ ║ You: Are you there?       ║           │                 │         │
│ ║ [Sent - User offline]     ║ ◄─────────│    Server lưu   │         │
│ ║                           ║           │    tin nhắn     │         │
│ ║ You: _                    ║           │    read=false   │         │
│ ╚═══════════════════════════╝           │                 │         │
│                                         └─────────────────┘         │
│                                                                     │
│                    ... Sau đó B login ...                           │
│                                                                     │
│                                  ╔═══════════════════════════╗     │
│                                  ║ LOGIN SUCCESSFUL          ║     │
│                                  ║ Welcome, Bob!             ║     │
│                                  ╠═══════════════════════════╣     │
│                                  ║ ╔═════════════════════╗   ║     │
│                                  ║ ║ 📬 You have 1       ║   ║ ◄───┼── Thông báo khi login
│                                  ║ ║ unread message(s)!  ║   ║     │
│                                  ║ ║                     ║   ║     │
│                                  ║ ║ Alice: Are you...   ║   ║     │
│                                  ║ ║                     ║   ║     │
│                                  ║ ║ Go to Chat to reply!║   ║     │
│                                  ║ ╚═════════════════════╝   ║     │
│                                  ╚═══════════════════════════╝     │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### 7.4 Trường hợp 4: Quick Reply với phím 'r'

```
┌─────────────────────────────────────────────────────────────────────┐
│                        QUICK REPLY FLOW                              │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│ BƯỚC 1: Nhận popup thông báo                                        │
│ ┌───────────────────────────────────────┐                           │
│ │ ╔═════════════════════════════════╗   │                           │
│ │ ║ 📬 NEW MESSAGE from Alice       ║   │                           │
│ │ ║ "Hello, are you free?"          ║   │                           │
│ │ ║ Press 'r' in menu to reply      ║   │                           │
│ │ ╚═════════════════════════════════╝   │                           │
│ │                                       │                           │
│ │ Enter your choice: r                  │ ◄── User nhấn 'r'         │
│ └───────────────────────────────────────┘                           │
│                                                                     │
│ BƯỚC 2: Mở chat trực tiếp với Alice                                 │
│ ┌───────────────────────────────────────┐                           │
│ │ ╔═════════════════════════════════╗   │                           │
│ │ ║ Chatting with: Alice            ║   │                           │
│ │ ╠═════════════════════════════════╣   │                           │
│ │ ║ --- Chat History ---            ║   │                           │
│ │ ║ Alice: Hello, are you free?     ║   │                           │
│ │ ║ --- End of History ---          ║   │                           │
│ │ ║                                 ║   │                           │
│ │ ║ You: Yes, what's up?            ║   │ ◄── Reply ngay            │
│ │ ║ [Delivered ✓]                   ║   │                           │
│ │ ║ You: _                          ║   │                           │
│ │ ╚═════════════════════════════════╝   │                           │
│ └───────────────────────────────────────┘                           │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 8. Đồng bộ hóa và Thread Safety

### 8.1 Các Mutex được sử dụng

#### Phía Server:
| Mutex | Bảo vệ | Sử dụng khi |
|-------|--------|-------------|
| `usersMutex` | `users`, `userById` | Đọc/ghi thông tin user, check online |
| `sessionsMutex` | `sessions` | Validate session |
| `chatMutex` | `chatMessages` | Lưu/đọc tin nhắn |

#### Phía Client:
| Mutex | Bảo vệ | Sử dụng khi |
|-------|--------|-------------|
| `socketMutex` | `clientSocket` | Gửi message qua socket |
| `responseQueueMutex` | `responseQueue` | Push/pop response |
| `printMutex` | Console output | In ra màn hình |
| `chatPartnerMutex` | `currentChatPartnerId` | Check/set đang chat với ai |
| `notificationMutex` | `pendingChatUserId` | Lưu/đọc thông tin pending |

### 8.2 Tránh Deadlock

**Nguyên tắc:**
1. Luôn lock mutex theo thứ tự cố định
2. Không hold mutex khi gọi hàm có thể block
3. Giữ critical section ngắn nhất có thể

**Ví dụ đúng:**
```cpp
// Lấy thông tin rồi giải phóng lock trước khi gửi network
std::string senderName;
{
    std::lock_guard<std::mutex> lock(usersMutex);
    senderName = userById[senderId]->fullname;
}  // Lock được giải phóng ở đây

// Sau đó mới gửi network (không hold lock)
send(socket, data, ...);
```

**Ví dụ sai (có thể deadlock):**
```cpp
// KHÔNG NÊN: Hold lock trong khi gửi network
std::lock_guard<std::mutex> lock(usersMutex);
std::string senderName = userById[senderId]->fullname;
send(socket, data, ...);  // Network có thể block!
```

### 8.3 Condition Variable cho Response Queue

```cpp
// Receive thread: Push response vào queue
{
    std::lock_guard<std::mutex> lock(responseQueueMutex);
    responseQueue.push(response);
}
responseCondition.notify_one();  // Báo cho main thread

// Main thread: Chờ response
std::string waitForResponse(int timeoutMs) {
    std::unique_lock<std::mutex> lock(responseQueueMutex);

    // Chờ có response hoặc timeout
    bool hasData = responseCondition.wait_for(
        lock,
        std::chrono::milliseconds(timeoutMs),
        []() { return !responseQueue.empty() || !running; }
    );

    if (!hasData || responseQueue.empty()) return "";

    std::string response = responseQueue.front();
    responseQueue.pop();
    return response;
}
```

**Tại sao dùng condition_variable?**
- Tránh busy-waiting (polling liên tục)
- CPU hiệu quả hơn
- Có timeout để tránh chờ vô hạn

---

## Tổng kết

### Điểm mạnh của thiết kế:
1. **Real-time**: Tin nhắn được gửi ngay khi recipient online
2. **Offline support**: Tin nhắn được lưu khi offline, thông báo khi online
3. **Thread-safe**: Sử dụng mutex và condition_variable đúng cách
4. **Non-blocking UI**: Background thread xử lý nhận message
5. **Quick reply**: Có thể reply nhanh từ menu chính

### Hạn chế:
1. **In-memory storage**: Mất dữ liệu khi server restart
2. **Không encryption**: Tin nhắn không được mã hóa
3. **Không có typing indicator**: Không biết người kia đang gõ
4. **Không có message status**: Chỉ có delivered, không có "seen"

### Cải tiến có thể:
1. Thêm database persistence (SQLite, MySQL)
2. Thêm end-to-end encryption
3. Thêm typing indicator
4. Thêm read receipt ("seen")
5. Thêm group chat

---

*Document created: December 2024*
*Version: 1.0*

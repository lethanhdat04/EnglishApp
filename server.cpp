/**
 * ============================================================================
 * ENGLISH LEARNING APP - SERVER (Enhanced Version)
 * ============================================================================
 * Server xử lý các request từ client theo giao thức đã thiết kế
 * Sử dụng POSIX socket, multithreading để xử lý nhiều client đồng thời
 *
 * Compile: g++ -std=c++17 -pthread -o server server.cpp
 * Run: ./server [port]
 * ============================================================================
 */

#include <iostream>
#include <string>
#include <cstring>
#include <map>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <ctime>
#include <random>
#include <unordered_map>

// POSIX socket headers
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>

// ============================================================================
// CẤU HÌNH SERVER
// ============================================================================
#define DEFAULT_PORT 8888
#define BUFFER_SIZE 65536
#define MAX_CLIENTS 100

// ============================================================================
// CẤU TRÚC DỮ LIỆU
// ============================================================================

// Thông tin người dùng
struct User {
    std::string userId;
    std::string fullname;
    std::string email;
    std::string password;
    std::string level;      // beginner, intermediate, advanced
    std::string role;       // student, teacher
    long long createdAt;
    bool online;
    int clientSocket;       // Socket của client đang kết nối
};

// Thông tin session
struct Session {
    std::string sessionToken;
    std::string userId;
    long long expiresAt;
};

// Thông tin bài học
struct Lesson {
    std::string lessonId;
    std::string title;
    std::string description;
    std::string topic;      // grammar, vocabulary, listening, speaking, reading, writing
    std::string level;
    int duration;
    std::string textContent;
    std::string videoUrl;
    std::string audioUrl;
};

// Thông tin câu hỏi test
struct TestQuestion {
    std::string questionId;
    std::string type;       // multiple_choice, fill_blank
    std::string question;
    std::vector<std::string> options;
    std::string correctAnswer;
    int points;
};

// Thông tin bài test
struct Test {
    std::string testId;
    std::string testType;
    std::string level;
    std::string topic;
    std::string title;
    std::vector<TestQuestion> questions;
};

// Thông tin tin nhắn chat
struct ChatMessage {
    std::string messageId;
    std::string senderId;
    std::string recipientId;
    std::string content;
    long long timestamp;
    bool read;
};

// ============================================================================
// BIẾN TOÀN CỤC VÀ MUTEX
// ============================================================================
std::map<std::string, User> users;              // email -> User (changed for easier lookup)
std::map<std::string, User*> userById;          // userId -> User*
std::map<std::string, Session> sessions;        // sessionToken -> Session
std::map<std::string, Lesson> lessons;          // lessonId -> Lesson
std::map<std::string, Test> tests;              // testId -> Test
std::vector<ChatMessage> chatMessages;          // Danh sách tin nhắn
std::map<int, std::string> clientSessions;      // socket -> sessionToken

std::mutex usersMutex;
std::mutex sessionsMutex;
std::mutex chatMutex;
std::mutex logMutex;

int serverSocket = -1;
bool running = true;

// ============================================================================
// HÀM TIỆN ÍCH
// ============================================================================

// Lấy timestamp hiện tại (milliseconds)
long long getCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// Sinh ID ngẫu nhiên
std::string generateId(const std::string& prefix) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(10000, 99999);
    return prefix + "_" + std::to_string(dis(gen));
}

// Sinh session token đơn giản
std::string generateSessionToken() {
    static const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string token;
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, sizeof(alphanum) - 2);

    for (int i = 0; i < 64; ++i) {
        token += alphanum[dis(gen)];
    }
    return token;
}

// Ghi log
void logMessage(const std::string& direction, const std::string& clientInfo, const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex);

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::string timeStr = std::ctime(&time);
    timeStr.pop_back();

    std::cout << "[" << timeStr << "] " << direction << " " << clientInfo << ": ";

    if (message.length() > 200) {
        std::cout << message.substr(0, 200) << "..." << std::endl;
    } else {
        std::cout << message << std::endl;
    }

    std::ofstream logFile("server.log", std::ios::app);
    if (logFile.is_open()) {
        logFile << "[" << timeStr << "] " << direction << " " << clientInfo << ": " << message << std::endl;
        logFile.close();
    }
}

// Escape JSON string
std::string escapeJson(const std::string& str) {
    std::string result;
    for (char c : str) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c;
        }
    }
    return result;
}

// ============================================================================
// JSON PARSER ĐƠN GIẢN
// ============================================================================

std::string getJsonValue(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) return "";

    size_t colonPos = json.find(':', keyPos);
    if (colonPos == std::string::npos) return "";

    size_t valueStart = colonPos + 1;
    while (valueStart < json.length() && (json[valueStart] == ' ' || json[valueStart] == '\t' || json[valueStart] == '\n')) {
        valueStart++;
    }

    if (valueStart >= json.length()) return "";

    if (json[valueStart] == '"') {
        size_t valueEnd = valueStart + 1;
        while (valueEnd < json.length()) {
            if (json[valueEnd] == '"' && json[valueEnd - 1] != '\\') {
                break;
            }
            valueEnd++;
        }
        if (valueEnd < json.length()) {
            return json.substr(valueStart + 1, valueEnd - valueStart - 1);
        }
    } else {
        size_t valueEnd = json.find_first_of(",}\n]", valueStart);
        if (valueEnd != std::string::npos) {
            std::string value = json.substr(valueStart, valueEnd - valueStart);
            value.erase(0, value.find_first_not_of(" \t\n\r"));
            value.erase(value.find_last_not_of(" \t\n\r") + 1);
            return value;
        }
    }

    return "";
}

std::string getJsonObject(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) return "";

    size_t colonPos = json.find(':', keyPos);
    if (colonPos == std::string::npos) return "";

    size_t bracePos = json.find('{', colonPos);
    if (bracePos == std::string::npos) return "";

    int braceCount = 1;
    size_t endPos = bracePos + 1;
    while (endPos < json.length() && braceCount > 0) {
        if (json[endPos] == '{') braceCount++;
        else if (json[endPos] == '}') braceCount--;
        endPos++;
    }

    return json.substr(bracePos, endPos - bracePos);
}

std::string getJsonArray(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) return "";

    size_t colonPos = json.find(':', keyPos);
    if (colonPos == std::string::npos) return "";

    size_t bracketPos = json.find('[', colonPos);
    if (bracketPos == std::string::npos) return "";

    int bracketCount = 1;
    size_t endPos = bracketPos + 1;
    while (endPos < json.length() && bracketCount > 0) {
        if (json[endPos] == '[') bracketCount++;
        else if (json[endPos] == ']') bracketCount--;
        endPos++;
    }

    return json.substr(bracketPos, endPos - bracketPos);
}

// ============================================================================
// KHỞI TẠO DỮ LIỆU MẪU - PHONG PHÚ
// ============================================================================

void initSampleData() {
    // ========== TẠO USERS ==========

    // Teacher 1
    User teacher1;
    teacher1.userId = "teacher_001";
    teacher1.fullname = "Ms. Sarah Johnson";
    teacher1.email = "sarah@example.com";
    teacher1.password = "teacher123";
    teacher1.role = "teacher";
    teacher1.level = "advanced";
    teacher1.createdAt = getCurrentTimestamp();
    teacher1.online = false;
    teacher1.clientSocket = -1;
    users[teacher1.email] = teacher1;
    userById[teacher1.userId] = &users[teacher1.email];

    // Teacher 2
    User teacher2;
    teacher2.userId = "teacher_002";
    teacher2.fullname = "Mr. John Smith";
    teacher2.email = "john@example.com";
    teacher2.password = "teacher123";
    teacher2.role = "teacher";
    teacher2.level = "advanced";
    teacher2.createdAt = getCurrentTimestamp();
    teacher2.online = false;
    teacher2.clientSocket = -1;
    users[teacher2.email] = teacher2;
    userById[teacher2.userId] = &users[teacher2.email];

    // Student 1
    User student1;
    student1.userId = "student_001";
    student1.fullname = "Nguyen Van A";
    student1.email = "student@example.com";
    student1.password = "student123";
    student1.role = "student";
    student1.level = "beginner";
    student1.createdAt = getCurrentTimestamp();
    student1.online = false;
    student1.clientSocket = -1;
    users[student1.email] = student1;
    userById[student1.userId] = &users[student1.email];

    // Student 2
    User student2;
    student2.userId = "student_002";
    student2.fullname = "Tran Thi B";
    student2.email = "student2@example.com";
    student2.password = "student123";
    student2.role = "student";
    student2.level = "intermediate";
    student2.createdAt = getCurrentTimestamp();
    student2.online = false;
    student2.clientSocket = -1;
    users[student2.email] = student2;
    userById[student2.userId] = &users[student2.email];

    // Student 3
    User student3;
    student3.userId = "student_003";
    student3.fullname = "Le Van C";
    student3.email = "student3@example.com";
    student3.password = "student123";
    student3.role = "student";
    student3.level = "advanced";
    student3.createdAt = getCurrentTimestamp();
    student3.online = false;
    student3.clientSocket = -1;
    users[student3.email] = student3;
    userById[student3.userId] = &users[student3.email];

    // ========== TẠO BÀI HỌC - BEGINNER ==========

    // Lesson 1: Present Simple
    Lesson lesson1;
    lesson1.lessonId = "lesson_001";
    lesson1.title = "Present Simple Tense";
    lesson1.description = "Learn how to use present simple tense in English";
    lesson1.topic = "grammar";
    lesson1.level = "beginner";
    lesson1.duration = 30;
    lesson1.textContent = R"(
========================================
        PRESENT SIMPLE TENSE
========================================

1. USAGE (Cách dùng)
--------------------
The present simple tense is used to describe:
- Habits and routines (Thói quen)
- General truths and facts (Sự thật chung)
- Fixed schedules (Lịch trình cố định)

2. STRUCTURE (Cấu trúc)
-----------------------
(+) Affirmative: Subject + V(s/es)
    - I/You/We/They + V
    - He/She/It + V(s/es)

(-) Negative: Subject + do/does + not + V
    - I/You/We/They + do not (don't) + V
    - He/She/It + does not (doesn't) + V

(?) Question: Do/Does + Subject + V?
    - Do + I/you/we/they + V?
    - Does + he/she/it + V?

3. EXAMPLES (Ví dụ)
-------------------
(+) I work in an office.
    He works in an office.
    She plays tennis every Sunday.

(-) I don't work on Sundays.
    She doesn't like coffee.
    They don't speak French.

(?) Do you work here?
    Does he play football?
    Do they live in London?

4. TIME EXPRESSIONS (Trạng từ thời gian)
----------------------------------------
- always (luôn luôn)
- usually (thường)
- often (thường xuyên)
- sometimes (thỉnh thoảng)
- rarely (hiếm khi)
- never (không bao giờ)
- every day/week/month/year

5. SPELLING RULES (Quy tắc chính tả)
------------------------------------
For he/she/it:
- Most verbs: add -s (work -> works)
- Verbs ending in -s, -sh, -ch, -x, -o: add -es
  (watch -> watches, go -> goes)
- Verbs ending in consonant + y: change y to -ies
  (study -> studies, fly -> flies)

6. COMMON MISTAKES (Lỗi thường gặp)
-----------------------------------
X He don't like coffee.
V He doesn't like coffee.

X She work in a bank.
V She works in a bank.

X Do she speak English?
V Does she speak English?
)";
    lessons[lesson1.lessonId] = lesson1;

    // Lesson 2: Common Daily Vocabulary
    Lesson lesson2;
    lesson2.lessonId = "lesson_002";
    lesson2.title = "Common Daily Vocabulary";
    lesson2.description = "Essential vocabulary for daily conversation";
    lesson2.topic = "vocabulary";
    lesson2.level = "beginner";
    lesson2.duration = 25;
    lesson2.textContent = R"(
========================================
     COMMON DAILY VOCABULARY
========================================

1. GREETINGS (Chào hỏi)
-----------------------
- Hello /həˈloʊ/ - Xin chào
- Hi /haɪ/ - Chào (thân mật)
- Good morning - Chào buổi sáng
- Good afternoon - Chào buổi chiều
- Good evening - Chào buổi tối
- Goodbye /ˌɡʊdˈbaɪ/ - Tạm biệt
- See you later - Hẹn gặp lại
- Good night - Chúc ngủ ngon

2. BASIC EXPRESSIONS (Câu giao tiếp cơ bản)
-------------------------------------------
- Thank you / Thanks - Cảm ơn
- You're welcome - Không có gì
- Please - Làm ơn / Xin vui lòng
- Sorry / Excuse me - Xin lỗi
- Yes - Vâng / Có
- No - Không

3. QUESTIONS (Câu hỏi)
----------------------
- How are you? - Bạn khỏe không?
- What's your name? - Tên bạn là gì?
- Where are you from? - Bạn đến từ đâu?
- What time is it? - Mấy giờ rồi?
- How old are you? - Bạn bao nhiêu tuổi?
- What do you do? - Bạn làm nghề gì?

4. NUMBERS (Số đếm)
-------------------
1 - one      6 - six
2 - two      7 - seven
3 - three    8 - eight
4 - four     9 - nine
5 - five     10 - ten

11 - eleven       20 - twenty
12 - twelve       30 - thirty
13 - thirteen     40 - forty
14 - fourteen     50 - fifty
15 - fifteen      100 - one hundred

5. COLORS (Màu sắc)
-------------------
- Red /red/ - Đỏ
- Blue /bluː/ - Xanh dương
- Green /ɡriːn/ - Xanh lá
- Yellow /ˈjeloʊ/ - Vàng
- Orange /ˈɔːrɪndʒ/ - Cam
- Purple /ˈpɜːrpl/ - Tím
- Pink /pɪŋk/ - Hồng
- White /waɪt/ - Trắng
- Black /blæk/ - Đen
- Brown /braʊn/ - Nâu
- Gray /ɡreɪ/ - Xám

6. DAYS OF THE WEEK (Các ngày trong tuần)
-----------------------------------------
- Monday - Thứ Hai
- Tuesday - Thứ Ba
- Wednesday - Thứ Tư
- Thursday - Thứ Năm
- Friday - Thứ Sáu
- Saturday - Thứ Bảy
- Sunday - Chủ Nhật

7. FAMILY MEMBERS (Thành viên gia đình)
---------------------------------------
- Father / Dad - Bố
- Mother / Mom - Mẹ
- Brother - Anh/Em trai
- Sister - Chị/Em gái
- Grandfather - Ông
- Grandmother - Bà
- Uncle - Chú/Bác/Cậu
- Aunt - Cô/Dì/Thím
)";
    lessons[lesson2.lessonId] = lesson2;

    // Lesson 3: Basic Listening Skills
    Lesson lesson3;
    lesson3.lessonId = "lesson_003";
    lesson3.title = "Introduction to Listening";
    lesson3.description = "Basic listening skills and strategies for beginners";
    lesson3.topic = "listening";
    lesson3.level = "beginner";
    lesson3.duration = 20;
    lesson3.textContent = R"(
========================================
    INTRODUCTION TO LISTENING
========================================

1. WHY IS LISTENING IMPORTANT?
------------------------------
- Understanding native speakers
- Improving pronunciation
- Learning natural expressions
- Building confidence in communication

2. LISTENING STRATEGIES
-----------------------
a) Before listening:
   - Read the questions first
   - Predict what you might hear
   - Focus on key words

b) While listening:
   - Don't panic if you miss something
   - Focus on main ideas first
   - Listen for key words
   - Pay attention to intonation

c) After listening:
   - Check your answers
   - Listen again if possible
   - Note new vocabulary

3. COMMON LISTENING SITUATIONS
------------------------------
- Introducing yourself
- Asking for directions
- Ordering food
- Shopping
- Making phone calls

4. PRACTICE DIALOGUE
--------------------
A: Hello! My name is Tom. What's your name?
B: Hi Tom! I'm Lisa. Nice to meet you.
A: Nice to meet you too. Where are you from?
B: I'm from Vietnam. How about you?
A: I'm from the USA. Do you like it here?
B: Yes, I do. It's very nice!

5. KEY PHRASES TO LISTEN FOR
----------------------------
- "My name is..." (Tên tôi là...)
- "I'm from..." (Tôi đến từ...)
- "Nice to meet you" (Rất vui được gặp bạn)
- "How are you?" (Bạn khỏe không?)
- "I'm fine, thank you" (Tôi khỏe, cảm ơn)

6. TIPS FOR IMPROVEMENT
-----------------------
- Listen to English every day (5-10 minutes)
- Watch English videos with subtitles
- Listen to slow, clear recordings first
- Repeat what you hear
- Don't translate word by word
)";
    lessons[lesson3.lessonId] = lesson3;

    // ========== TẠO BÀI HỌC - INTERMEDIATE ==========

    // Lesson 4: Past Tenses
    Lesson lesson4;
    lesson4.lessonId = "lesson_004";
    lesson4.title = "Past Tenses";
    lesson4.description = "Master past simple and past continuous tenses";
    lesson4.topic = "grammar";
    lesson4.level = "intermediate";
    lesson4.duration = 45;
    lesson4.textContent = R"(
========================================
          PAST TENSES
========================================

1. PAST SIMPLE TENSE
--------------------
Used for completed actions in the past.

Structure:
(+) Subject + V2 (past form)
(-) Subject + did not (didn't) + V
(?) Did + Subject + V?

Regular verbs: add -ed
- work -> worked
- play -> played
- study -> studied

Irregular verbs (must memorize):
- go -> went
- see -> saw
- eat -> ate
- buy -> bought
- come -> came
- take -> took
- make -> made
- give -> gave
- find -> found
- know -> knew

Examples:
- I worked late yesterday.
- She didn't go to the party.
- Did you see the movie?

2. PAST CONTINUOUS TENSE
------------------------
Used for ongoing actions in the past.

Structure:
(+) Subject + was/were + V-ing
(-) Subject + was/were + not + V-ing
(?) Was/Were + Subject + V-ing?

Examples:
- I was studying at 8 PM.
- They were playing football.
- Was she working when you called?

3. PAST SIMPLE vs PAST CONTINUOUS
---------------------------------
Use Past Continuous for longer/background actions
Use Past Simple for shorter/interrupting actions

Example:
"I was cooking when the phone rang."
- was cooking = longer action (past continuous)
- rang = shorter, interrupting action (past simple)

More examples:
- While she was sleeping, someone knocked on the door.
- They were watching TV when the power went out.
- I met her while I was walking in the park.

4. TIME EXPRESSIONS
-------------------
Past Simple:
- yesterday
- last week/month/year
- two days ago
- in 2020

Past Continuous:
- at 8 o'clock yesterday
- this time last week
- while
- when

5. COMMON MISTAKES
------------------
X I was go to school yesterday.
V I went to school yesterday.

X She didn't went to the party.
V She didn't go to the party.

X When I was walking home, I was seeing a cat.
V When I was walking home, I saw a cat.
)";
    lessons[lesson4.lessonId] = lesson4;

    // Lesson 5: Business Vocabulary
    Lesson lesson5;
    lesson5.lessonId = "lesson_005";
    lesson5.title = "Business English Vocabulary";
    lesson5.description = "Essential vocabulary for the workplace";
    lesson5.topic = "vocabulary";
    lesson5.level = "intermediate";
    lesson5.duration = 35;
    lesson5.textContent = R"(
========================================
    BUSINESS ENGLISH VOCABULARY
========================================

1. OFFICE VOCABULARY
--------------------
- Meeting - Cuộc họp
- Deadline - Hạn chót
- Report - Báo cáo
- Presentation - Bài thuyết trình
- Conference - Hội nghị
- Department - Phòng ban
- Manager - Quản lý
- Employee - Nhân viên
- Colleague - Đồng nghiệp
- Client - Khách hàng

2. EMAIL EXPRESSIONS
--------------------
Opening:
- Dear Mr./Ms. [Name],
- Hello [Name],
- Good morning/afternoon,

Body:
- I am writing to inform you...
- Please find attached...
- I would like to request...
- Thank you for your email.
- Regarding your inquiry...

Closing:
- Best regards,
- Kind regards,
- Sincerely,
- Looking forward to hearing from you.

3. MEETING PHRASES
------------------
Starting:
- Let's get started.
- The purpose of this meeting is...
- Today we're going to discuss...

Opinions:
- In my opinion...
- I think/believe that...
- From my point of view...

Agreeing:
- I agree with you.
- That's a good point.
- Exactly!

Disagreeing:
- I see your point, but...
- I'm not sure I agree.
- I have a different opinion.

Ending:
- To summarize...
- Let's wrap up.
- Any final questions?

4. PHONE CONVERSATIONS
----------------------
Answering:
- Hello, [Company name], how may I help you?
- [Name] speaking.

Asking to speak:
- May I speak to [Name], please?
- Could you put me through to...?
- Is [Name] available?

Leaving a message:
- Could you take a message?
- Please tell him/her that...
- Could you ask him/her to call me back?

5. COMMON BUSINESS VERBS
------------------------
- negotiate - đàm phán
- collaborate - hợp tác
- implement - triển khai
- analyze - phân tích
- delegate - ủy quyền
- prioritize - ưu tiên
- schedule - lên lịch
- confirm - xác nhận
)";
    lessons[lesson5.lessonId] = lesson5;

    // ========== TẠO BÀI HỌC - ADVANCED ==========

    // Lesson 6: Advanced Grammar - Conditionals
    Lesson lesson6;
    lesson6.lessonId = "lesson_006";
    lesson6.title = "Conditional Sentences";
    lesson6.description = "Master all types of conditional sentences";
    lesson6.topic = "grammar";
    lesson6.level = "advanced";
    lesson6.duration = 50;
    lesson6.textContent = R"(
========================================
      CONDITIONAL SENTENCES
========================================

1. ZERO CONDITIONAL (Type 0)
----------------------------
Use: General truths, scientific facts

Structure: If + present simple, present simple

Examples:
- If you heat water to 100°C, it boils.
- If it rains, the grass gets wet.
- Plants die if they don't get water.

2. FIRST CONDITIONAL (Type 1)
-----------------------------
Use: Real/possible situations in the future

Structure: If + present simple, will + V

Examples:
- If it rains tomorrow, I will stay home.
- If you study hard, you will pass the exam.
- She will be angry if you don't call her.

3. SECOND CONDITIONAL (Type 2)
------------------------------
Use: Unreal/hypothetical situations now

Structure: If + past simple, would + V

Examples:
- If I won the lottery, I would buy a house.
- If I were you, I would accept the job.
- She would travel more if she had more money.

Note: Use "were" for all subjects (formal)
- If I were rich... (not "was")
- If she were here...

4. THIRD CONDITIONAL (Type 3)
-----------------------------
Use: Unreal situations in the past

Structure: If + past perfect, would have + V3

Examples:
- If I had studied harder, I would have passed.
- If she had left earlier, she wouldn't have missed the train.
- They would have won if they had practiced more.

5. MIXED CONDITIONALS
---------------------
Type 3 + Type 2 (Past -> Present):
If + past perfect, would + V
- If I had taken that job, I would be rich now.

Type 2 + Type 3 (Present -> Past):
If + past simple, would have + V3
- If I were braver, I would have asked her out.

6. ALTERNATIVE STRUCTURES
-------------------------
Unless = If...not
- Unless you hurry, you'll be late.
- (= If you don't hurry, you'll be late.)

Provided that / As long as
- I'll help you provided that you help me too.

In case
- Take an umbrella in case it rains.

7. COMMON MISTAKES
------------------
X If I will see him, I will tell him.
V If I see him, I will tell him.

X If I would have money, I would buy it.
V If I had money, I would buy it.

X If I would have known, I would have helped.
V If I had known, I would have helped.
)";
    lessons[lesson6.lessonId] = lesson6;

    // Lesson 7: IELTS Speaking
    Lesson lesson7;
    lesson7.lessonId = "lesson_007";
    lesson7.title = "IELTS Speaking Skills";
    lesson7.description = "Advanced speaking techniques for IELTS exam";
    lesson7.topic = "speaking";
    lesson7.level = "advanced";
    lesson7.duration = 60;
    lesson7.textContent = R"(
========================================
      IELTS SPEAKING SKILLS
========================================

1. PART 1: INTRODUCTION (4-5 minutes)
-------------------------------------
Topics: Home, work, studies, hobbies, etc.

Tips:
- Give extended answers (2-3 sentences)
- Don't memorize scripts
- Be natural and confident

Example:
Q: Do you work or study?
A: I'm currently working as a software developer
   at a tech company in Ho Chi Minh City. I've been
   in this position for about two years, and I find
   it quite challenging but rewarding.

2. PART 2: LONG TURN (3-4 minutes)
----------------------------------
You get a cue card with a topic.
1 minute to prepare, 2 minutes to speak.

Structure:
- Introduction: What it is
- Description: Details
- Explanation: Why/How
- Conclusion: Your feelings/opinions

Example topic: Describe a book you enjoyed reading.
- What book it was
- When you read it
- What it was about
- Why you enjoyed it

3. PART 3: DISCUSSION (4-5 minutes)
-----------------------------------
Abstract questions related to Part 2.

Tips:
- Express and justify opinions
- Give examples
- Consider different perspectives
- Use advanced vocabulary

4. USEFUL PHRASES
-----------------
Giving opinions:
- From my perspective...
- As far as I'm concerned...
- I would argue that...
- In my view...

Explaining:
- The main reason is that...
- This is primarily because...
- What I mean by that is...

Giving examples:
- For instance...
- A good example would be...
- To illustrate this point...

Contrasting:
- On the other hand...
- Having said that...
- Nevertheless...

5. FLUENCY TECHNIQUES
---------------------
- Use fillers naturally: "Well...", "Let me think..."
- Paraphrase if you forget a word
- Self-correct naturally
- Maintain eye contact
- Speak at a natural pace

6. VOCABULARY ENHANCEMENT
-------------------------
Instead of "good" -> excellent, outstanding, remarkable
Instead of "bad" -> terrible, dreadful, appalling
Instead of "big" -> enormous, massive, substantial
Instead of "small" -> tiny, minute, negligible
Instead of "important" -> crucial, vital, significant
)";
    lessons[lesson7.lessonId] = lesson7;

    // ========== TẠO BÀI TEST ==========

    // Test 1: Beginner Grammar
    Test test1;
    test1.testId = "test_001";
    test1.testType = "mixed";
    test1.level = "beginner";
    test1.topic = "grammar";
    test1.title = "Present Simple Tense Test";

    TestQuestion q1;
    q1.questionId = "q_001";
    q1.type = "multiple_choice";
    q1.question = "She ____ to school every day.";
    q1.options = {"go", "goes", "going", "went"};
    q1.correctAnswer = "b";
    q1.points = 10;
    test1.questions.push_back(q1);

    TestQuestion q2;
    q2.questionId = "q_002";
    q2.type = "multiple_choice";
    q2.question = "They ____ like spicy food.";
    q2.options = {"doesn't", "don't", "isn't", "aren't"};
    q2.correctAnswer = "b";
    q2.points = 10;
    test1.questions.push_back(q2);

    TestQuestion q3;
    q3.questionId = "q_003";
    q3.type = "fill_blank";
    q3.question = "I ____ English every day. (study)";
    q3.correctAnswer = "study";
    q3.points = 10;
    test1.questions.push_back(q3);

    TestQuestion q4;
    q4.questionId = "q_004";
    q4.type = "multiple_choice";
    q4.question = "Choose the correct sentence:";
    q4.options = {"He don't like coffee", "He doesn't likes coffee", "He doesn't like coffee", "He not like coffee"};
    q4.correctAnswer = "c";
    q4.points = 10;
    test1.questions.push_back(q4);

    TestQuestion q5;
    q5.questionId = "q_005";
    q5.type = "fill_blank";
    q5.question = "My mother ____ (cook) dinner every evening.";
    q5.correctAnswer = "cooks";
    q5.points = 10;
    test1.questions.push_back(q5);

    TestQuestion q6;
    q6.questionId = "q_006";
    q6.type = "multiple_choice";
    q6.question = "____ your brother work here?";
    q6.options = {"Do", "Does", "Is", "Are"};
    q6.correctAnswer = "b";
    q6.points = 10;
    test1.questions.push_back(q6);

    TestQuestion q7;
    q7.questionId = "q_007";
    q7.type = "multiple_choice";
    q7.question = "Water ____ at 100 degrees Celsius.";
    q7.options = {"boil", "boils", "boiling", "boiled"};
    q7.correctAnswer = "b";
    q7.points = 10;
    test1.questions.push_back(q7);

    TestQuestion q8;
    q8.questionId = "q_008";
    q8.type = "fill_blank";
    q8.question = "She always ____ (arrive) on time.";
    q8.correctAnswer = "arrives";
    q8.points = 10;
    test1.questions.push_back(q8);

    tests[test1.testId] = test1;

    // Test 2: Intermediate Grammar
    Test test2;
    test2.testId = "test_002";
    test2.testType = "mixed";
    test2.level = "intermediate";
    test2.topic = "grammar";
    test2.title = "Past Tenses Test";

    TestQuestion q2_1;
    q2_1.questionId = "q2_001";
    q2_1.type = "multiple_choice";
    q2_1.question = "I ____ to the cinema yesterday.";
    q2_1.options = {"go", "went", "gone", "going"};
    q2_1.correctAnswer = "b";
    q2_1.points = 10;
    test2.questions.push_back(q2_1);

    TestQuestion q2_2;
    q2_2.questionId = "q2_002";
    q2_2.type = "multiple_choice";
    q2_2.question = "While I ____ TV, the phone rang.";
    q2_2.options = {"watch", "watched", "was watching", "am watching"};
    q2_2.correctAnswer = "c";
    q2_2.points = 10;
    test2.questions.push_back(q2_2);

    TestQuestion q2_3;
    q2_3.questionId = "q2_003";
    q2_3.type = "fill_blank";
    q2_3.question = "She ____ (not/come) to the party last night.";
    q2_3.correctAnswer = "didn't come";
    q2_3.points = 10;
    test2.questions.push_back(q2_3);

    TestQuestion q2_4;
    q2_4.questionId = "q2_004";
    q2_4.type = "multiple_choice";
    q2_4.question = "They ____ football when it started to rain.";
    q2_4.options = {"played", "play", "were playing", "are playing"};
    q2_4.correctAnswer = "c";
    q2_4.points = 10;
    test2.questions.push_back(q2_4);

    TestQuestion q2_5;
    q2_5.questionId = "q2_005";
    q2_5.type = "fill_blank";
    q2_5.question = "What ____ you ____ (do) at 8pm yesterday?";
    q2_5.correctAnswer = "were doing";
    q2_5.points = 10;
    test2.questions.push_back(q2_5);

    tests[test2.testId] = test2;

    // Test 3: Advanced Grammar - Conditionals
    Test test3;
    test3.testId = "test_003";
    test3.testType = "mixed";
    test3.level = "advanced";
    test3.topic = "grammar";
    test3.title = "Conditional Sentences Test";

    TestQuestion q3_1;
    q3_1.questionId = "q3_001";
    q3_1.type = "multiple_choice";
    q3_1.question = "If I ____ rich, I would buy a big house.";
    q3_1.options = {"am", "was", "were", "will be"};
    q3_1.correctAnswer = "c";
    q3_1.points = 10;
    test3.questions.push_back(q3_1);

    TestQuestion q3_2;
    q3_2.questionId = "q3_002";
    q3_2.type = "multiple_choice";
    q3_2.question = "If you heat ice, it ____.";
    q3_2.options = {"melts", "will melt", "would melt", "melted"};
    q3_2.correctAnswer = "a";
    q3_2.points = 10;
    test3.questions.push_back(q3_2);

    TestQuestion q3_3;
    q3_3.questionId = "q3_003";
    q3_3.type = "fill_blank";
    q3_3.question = "If I had studied harder, I ____ (pass) the exam.";
    q3_3.correctAnswer = "would have passed";
    q3_3.points = 15;
    test3.questions.push_back(q3_3);

    TestQuestion q3_4;
    q3_4.questionId = "q3_004";
    q3_4.type = "multiple_choice";
    q3_4.question = "If it rains tomorrow, we ____ the picnic.";
    q3_4.options = {"cancel", "will cancel", "would cancel", "cancelled"};
    q3_4.correctAnswer = "b";
    q3_4.points = 10;
    test3.questions.push_back(q3_4);

    TestQuestion q3_5;
    q3_5.questionId = "q3_005";
    q3_5.type = "fill_blank";
    q3_5.question = "I wish I ____ (know) the answer.";
    q3_5.correctAnswer = "knew";
    q3_5.points = 15;
    test3.questions.push_back(q3_5);

    tests[test3.testId] = test3;

    std::cout << "[INFO] Sample data initialized: "
              << users.size() << " users, "
              << lessons.size() << " lessons, "
              << tests.size() << " tests" << std::endl;
}

// ============================================================================
// XỬ LÝ CÁC LOẠI REQUEST
// ============================================================================

// Xử lý REGISTER_REQUEST
std::string handleRegister(const std::string& json) {
    std::string payload = getJsonObject(json, "payload");
    std::string messageId = getJsonValue(json, "messageId");
    std::string fullname = getJsonValue(payload, "fullname");
    std::string email = getJsonValue(payload, "email");
    std::string password = getJsonValue(payload, "password");
    std::string confirmPassword = getJsonValue(payload, "confirmPassword");

    if (password != confirmPassword) {
        return R"({"messageType":"REGISTER_RESPONSE","messageId":")" + messageId +
               R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
               R"(,"payload":{"status":"error","message":"Passwords do not match"}})";
    }

    {
        std::lock_guard<std::mutex> lock(usersMutex);
        if (users.find(email) != users.end()) {
            return R"({"messageType":"REGISTER_RESPONSE","messageId":")" + messageId +
                   R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
                   R"(,"payload":{"status":"error","message":"Email already exists"}})";
        }

        User newUser;
        newUser.userId = generateId("user");
        newUser.fullname = fullname;
        newUser.email = email;
        newUser.password = password;
        newUser.role = "student";
        newUser.level = "beginner";
        newUser.createdAt = getCurrentTimestamp();
        newUser.online = false;
        newUser.clientSocket = -1;

        users[email] = newUser;
        userById[newUser.userId] = &users[email];

        return R"({"messageType":"REGISTER_RESPONSE","messageId":")" + messageId +
               R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
               R"(,"payload":{"status":"success","message":"Register successfully","data":{"userId":")" +
               newUser.userId + R"(","fullname":")" + escapeJson(newUser.fullname) +
               R"(","email":")" + newUser.email + R"(","createdAt":)" + std::to_string(newUser.createdAt) + R"(}}})";
    }
}

// Xử lý LOGIN_REQUEST
std::string handleLogin(const std::string& json, int clientSocket) {
    std::string payload = getJsonObject(json, "payload");
    std::string messageId = getJsonValue(json, "messageId");
    std::string email = getJsonValue(payload, "email");
    std::string password = getJsonValue(payload, "password");

    std::lock_guard<std::mutex> lock(usersMutex);

    auto it = users.find(email);
    if (it == users.end() || it->second.password != password) {
        return R"({"messageType":"LOGIN_RESPONSE","messageId":")" + messageId +
               R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
               R"(,"payload":{"status":"error","message":"Invalid email or password"}})";
    }

    User& user = it->second;
    user.online = true;
    user.clientSocket = clientSocket;

    std::string sessionToken = generateSessionToken();
    Session session;
    session.sessionToken = sessionToken;
    session.userId = user.userId;
    session.expiresAt = getCurrentTimestamp() + 3600000;

    {
        std::lock_guard<std::mutex> slock(sessionsMutex);
        sessions[sessionToken] = session;
        clientSessions[clientSocket] = sessionToken;
    }

    return R"({"messageType":"LOGIN_RESPONSE","messageId":")" + messageId +
           R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
           R"(,"payload":{"status":"success","message":"Login successfully","data":{"userId":")" +
           user.userId + R"(","fullname":")" + escapeJson(user.fullname) + R"(","email":")" +
           user.email + R"(","level":")" + user.level + R"(","sessionToken":")" +
           sessionToken + R"(","expiresAt":)" + std::to_string(session.expiresAt) + R"(}}})";
}

// Kiểm tra session hợp lệ
std::string validateSession(const std::string& sessionToken) {
    std::lock_guard<std::mutex> lock(sessionsMutex);
    auto it = sessions.find(sessionToken);
    if (it == sessions.end()) return "";
    if (it->second.expiresAt < getCurrentTimestamp()) {
        sessions.erase(it);
        return "";
    }
    return it->second.userId;
}

// Xử lý GET_LESSONS_REQUEST
std::string handleGetLessons(const std::string& json) {
    std::string payload = getJsonObject(json, "payload");
    std::string messageId = getJsonValue(json, "messageId");
    std::string sessionToken = getJsonValue(json, "sessionToken");
    std::string level = getJsonValue(payload, "level");
    std::string topic = getJsonValue(payload, "topic");

    std::string userId = validateSession(sessionToken);
    if (userId.empty()) {
        return R"({"messageType":"GET_LESSONS_RESPONSE","messageId":")" + messageId +
               R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
               R"(,"payload":{"status":"error","message":"Invalid or expired session"}})";
    }

    std::stringstream lessonsJson;
    lessonsJson << "[";
    bool first = true;
    int count = 0;

    for (const auto& pair : lessons) {
        const Lesson& lesson = pair.second;

        // Lọc theo topic nếu có
        if (!topic.empty() && lesson.topic != topic) continue;
        // Không lọc theo level để hiển thị tất cả

        if (!first) lessonsJson << ",";
        first = false;

        lessonsJson << R"({"lessonId":")" << lesson.lessonId
                    << R"(","title":")" << escapeJson(lesson.title)
                    << R"(","description":")" << escapeJson(lesson.description)
                    << R"(","topic":")" << lesson.topic
                    << R"(","level":")" << lesson.level
                    << R"(","duration":)" << lesson.duration
                    << R"(,"completionStatus":false,"progress":0})";
        count++;
    }
    lessonsJson << "]";

    return R"({"messageType":"GET_LESSONS_RESPONSE","messageId":")" + messageId +
           R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
           R"(,"payload":{"status":"success","message":"Retrieved lessons successfully","data":{"lessons":)" +
           lessonsJson.str() + R"(,"pagination":{"currentPage":1,"totalPages":1,"totalLessons":)" +
           std::to_string(count) + R"(}}}})";
}

// Xử lý GET_LESSON_DETAIL_REQUEST
std::string handleGetLessonDetail(const std::string& json) {
    std::string payload = getJsonObject(json, "payload");
    std::string messageId = getJsonValue(json, "messageId");
    std::string sessionToken = getJsonValue(json, "sessionToken");
    std::string lessonId = getJsonValue(payload, "lessonId");

    std::string userId = validateSession(sessionToken);
    if (userId.empty()) {
        return R"({"messageType":"GET_LESSON_DETAIL_RESPONSE","messageId":")" + messageId +
               R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
               R"(,"payload":{"status":"error","message":"Invalid or expired session"}})";
    }

    auto it = lessons.find(lessonId);
    if (it == lessons.end()) {
        return R"({"messageType":"GET_LESSON_DETAIL_RESPONSE","messageId":")" + messageId +
               R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
               R"(,"payload":{"status":"error","message":"Lesson not found"}})";
    }

    const Lesson& lesson = it->second;

    return R"({"messageType":"GET_LESSON_DETAIL_RESPONSE","messageId":")" + messageId +
           R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
           R"(,"payload":{"status":"success","data":{"lessonId":")" + lesson.lessonId +
           R"(","title":")" + escapeJson(lesson.title) +
           R"(","description":")" + escapeJson(lesson.description) +
           R"(","level":")" + lesson.level +
           R"(","topic":")" + lesson.topic +
           R"(","duration":)" + std::to_string(lesson.duration) +
           R"(,"textContent":")" + escapeJson(lesson.textContent) +
           R"(","videoUrl":"[Simulated Video]","audioUrl":"[Simulated Audio]"}}})";
}

// Xử lý GET_TEST_REQUEST
std::string handleGetTest(const std::string& json) {
    std::string payload = getJsonObject(json, "payload");
    std::string messageId = getJsonValue(json, "messageId");
    std::string sessionToken = getJsonValue(json, "sessionToken");
    std::string level = getJsonValue(payload, "level");

    std::string userId = validateSession(sessionToken);
    if (userId.empty()) {
        return R"({"messageType":"GET_TEST_RESPONSE","messageId":")" + messageId +
               R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
               R"(,"payload":{"status":"error","message":"Invalid or expired session"}})";
    }

    // Tìm test phù hợp với level
    const Test* selectedTest = nullptr;
    for (const auto& pair : tests) {
        if (pair.second.level == level) {
            selectedTest = &pair.second;
            break;
        }
    }

    // Nếu không tìm thấy, lấy test đầu tiên
    if (!selectedTest && !tests.empty()) {
        selectedTest = &tests.begin()->second;
    }

    if (!selectedTest) {
        return R"({"messageType":"GET_TEST_RESPONSE","messageId":")" + messageId +
               R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
               R"(,"payload":{"status":"error","message":"No tests available"}})";
    }

    std::stringstream questionsJson;
    questionsJson << "[";
    for (size_t i = 0; i < selectedTest->questions.size(); i++) {
        const TestQuestion& q = selectedTest->questions[i];
        if (i > 0) questionsJson << ",";

        questionsJson << R"({"questionId":")" << q.questionId
                      << R"(","type":")" << q.type
                      << R"(","order":)" << (i + 1)
                      << R"(,"question":")" << escapeJson(q.question)
                      << R"(","points":)" << q.points;

        if (!q.options.empty()) {
            questionsJson << R"(,"options":[)";
            for (size_t j = 0; j < q.options.size(); j++) {
                if (j > 0) questionsJson << ",";
                char optionId = 'a' + j;
                questionsJson << R"({"id":")" << optionId << R"(","text":")" << escapeJson(q.options[j]) << R"("})";
            }
            questionsJson << "]";
        }
        questionsJson << "}";
    }
    questionsJson << "]";

    return R"({"messageType":"GET_TEST_RESPONSE","messageId":")" + messageId +
           R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
           R"(,"payload":{"status":"success","data":{"testId":")" + selectedTest->testId +
           R"(","testType":")" + selectedTest->testType +
           R"(","level":")" + selectedTest->level +
           R"(","topic":")" + selectedTest->topic +
           R"(","title":")" + escapeJson(selectedTest->title) +
           R"(","duration":1800,"totalQuestions":)" + std::to_string(selectedTest->questions.size()) +
           R"(,"passingScore":60,"questions":)" + questionsJson.str() +
           R"(,"instructions":"Read each question carefully. Answer all questions."}}})";
}

// Xử lý SUBMIT_TEST_REQUEST
std::string handleSubmitTest(const std::string& json) {
    std::string payload = getJsonObject(json, "payload");
    std::string messageId = getJsonValue(json, "messageId");
    std::string sessionToken = getJsonValue(json, "sessionToken");
    std::string testId = getJsonValue(payload, "testId");

    std::string userId = validateSession(sessionToken);
    if (userId.empty()) {
        return R"({"messageType":"SUBMIT_TEST_RESPONSE","messageId":")" + messageId +
               R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
               R"(,"payload":{"status":"error","message":"Invalid or expired session"}})";
    }

    auto it = tests.find(testId);
    if (it == tests.end()) {
        return R"({"messageType":"SUBMIT_TEST_RESPONSE","messageId":")" + messageId +
               R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
               R"(,"payload":{"status":"error","message":"Test not found"}})";
    }

    const Test& test = it->second;
    std::string answersArray = getJsonArray(json, "answers");

    int totalPoints = 0;
    int earnedPoints = 0;
    int correctCount = 0;
    int wrongCount = 0;

    std::stringstream detailedResults;
    detailedResults << "[";

    for (size_t i = 0; i < test.questions.size(); i++) {
        const TestQuestion& q = test.questions[i];
        totalPoints += q.points;

        // Tìm câu trả lời của user cho câu hỏi này
        std::string userAnswer = "";
        size_t qPos = answersArray.find(q.questionId);
        if (qPos != std::string::npos) {
            size_t answerPos = answersArray.find("\"answer\"", qPos);
            if (answerPos != std::string::npos) {
                size_t colonPos = answersArray.find(':', answerPos);
                size_t valueStart = answersArray.find('"', colonPos);
                size_t valueEnd = answersArray.find('"', valueStart + 1);
                if (valueStart != std::string::npos && valueEnd != std::string::npos) {
                    userAnswer = answersArray.substr(valueStart + 1, valueEnd - valueStart - 1);
                }
            }
        }

        bool isCorrect = false;
        if (q.type == "multiple_choice") {
            isCorrect = (userAnswer == q.correctAnswer);
        } else {
            // fill_blank: so sánh không phân biệt hoa thường và khoảng trắng thừa
            std::string lowerUser = userAnswer;
            std::string lowerCorrect = q.correctAnswer;
            std::transform(lowerUser.begin(), lowerUser.end(), lowerUser.begin(), ::tolower);
            std::transform(lowerCorrect.begin(), lowerCorrect.end(), lowerCorrect.begin(), ::tolower);
            // Trim
            lowerUser.erase(0, lowerUser.find_first_not_of(" \t"));
            lowerUser.erase(lowerUser.find_last_not_of(" \t") + 1);
            lowerCorrect.erase(0, lowerCorrect.find_first_not_of(" \t"));
            lowerCorrect.erase(lowerCorrect.find_last_not_of(" \t") + 1);
            isCorrect = (lowerUser == lowerCorrect);
        }

        if (isCorrect) {
            earnedPoints += q.points;
            correctCount++;
        } else {
            wrongCount++;
        }

        if (i > 0) detailedResults << ",";
        detailedResults << R"({"questionId":")" << q.questionId
                        << R"(","correct":)" << (isCorrect ? "true" : "false")
                        << R"(,"userAnswer":")" << escapeJson(userAnswer)
                        << R"(","correctAnswer":")" << escapeJson(q.correctAnswer)
                        << R"(","points":)" << q.points
                        << R"(,"earnedPoints":)" << (isCorrect ? q.points : 0) << "}";
    }
    detailedResults << "]";

    int percentage = (totalPoints > 0) ? (earnedPoints * 100 / totalPoints) : 0;
    bool passed = (percentage >= 60);
    std::string grade;
    if (percentage >= 90) grade = "A";
    else if (percentage >= 80) grade = "B";
    else if (percentage >= 70) grade = "C";
    else if (percentage >= 60) grade = "D";
    else grade = "F";

    return R"({"messageType":"SUBMIT_TEST_RESPONSE","messageId":")" + messageId +
           R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
           R"(,"payload":{"status":"success","data":{"testId":")" + testId +
           R"(","submissionId":")" + generateId("sub") +
           R"(","score":)" + std::to_string(earnedPoints) +
           R"(,"percentage":)" + std::to_string(percentage) +
           R"(,"totalPoints":)" + std::to_string(totalPoints) +
           R"(,"earnedPoints":)" + std::to_string(earnedPoints) +
           R"(,"totalQuestions":)" + std::to_string(test.questions.size()) +
           R"(,"correctAnswers":)" + std::to_string(correctCount) +
           R"(,"wrongAnswers":)" + std::to_string(wrongCount) +
           R"(,"passed":)" + (passed ? "true" : "false") +
           R"(,"grade":")" + grade +
           R"(","detailedResults":)" + detailedResults.str() + R"(}}})";
}

// Xử lý GET_CONTACT_LIST_REQUEST
std::string handleGetContactList(const std::string& json) {
    std::string messageId = getJsonValue(json, "messageId");
    std::string sessionToken = getJsonValue(json, "sessionToken");

    std::string currentUserId = validateSession(sessionToken);
    if (currentUserId.empty()) {
        return R"({"messageType":"GET_CONTACT_LIST_RESPONSE","messageId":")" + messageId +
               R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
               R"(,"payload":{"status":"error","message":"Invalid or expired session"}})";
    }

    std::stringstream contactsJson;
    contactsJson << "[";
    bool first = true;
    int onlineCount = 0;

    {
        std::lock_guard<std::mutex> lock(usersMutex);
        for (const auto& pair : users) {
            const User& user = pair.second;
            if (user.userId == currentUserId) continue;

            if (!first) contactsJson << ",";
            first = false;

            contactsJson << R"({"userId":")" << user.userId
                         << R"(","fullName":")" << escapeJson(user.fullname)
                         << R"(","role":")" << user.role
                         << R"(","status":")" << (user.online ? "online" : "offline")
                         << R"(","level":")" << user.level << R"("})";

            if (user.online) onlineCount++;
        }
    }
    contactsJson << "]";

    return R"({"messageType":"GET_CONTACT_LIST_RESPONSE","messageId":")" + messageId +
           R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
           R"(,"payload":{"status":"success","data":{"contacts":)" + contactsJson.str() +
           R"(,"onlineCount":)" + std::to_string(onlineCount) +
           R"(,"totalContacts":)" + std::to_string(users.size() - 1) + R"(}}})";
}

// Xử lý SEND_MESSAGE_REQUEST
std::string handleSendMessage(const std::string& json, int senderSocket) {
    std::string payload = getJsonObject(json, "payload");
    std::string messageId = getJsonValue(json, "messageId");
    std::string sessionToken = getJsonValue(json, "sessionToken");
    std::string recipientId = getJsonValue(payload, "recipientId");
    std::string messageContent = getJsonValue(payload, "messageContent");

    std::string senderId = validateSession(sessionToken);
    if (senderId.empty()) {
        return R"({"messageType":"SEND_MESSAGE_RESPONSE","messageId":")" + messageId +
               R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
               R"(,"payload":{"status":"error","message":"Invalid or expired session"}})";
    }

    User* recipient = nullptr;
    std::string senderName;
    {
        std::lock_guard<std::mutex> lock(usersMutex);
        auto it = userById.find(recipientId);
        if (it != userById.end()) {
            recipient = it->second;
        }
        auto senderIt = userById.find(senderId);
        if (senderIt != userById.end()) {
            senderName = senderIt->second->fullname;
        }
    }

    if (!recipient) {
        return R"({"messageType":"SEND_MESSAGE_RESPONSE","messageId":")" + messageId +
               R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
               R"(,"payload":{"status":"error","message":"Recipient not found"}})";
    }

    ChatMessage msg;
    msg.messageId = generateId("chatmsg");
    msg.senderId = senderId;
    msg.recipientId = recipientId;
    msg.content = messageContent;
    msg.timestamp = getCurrentTimestamp();
    msg.read = false;

    {
        std::lock_guard<std::mutex> lock(chatMutex);
        chatMessages.push_back(msg);
    }

    bool delivered = false;
    if (recipient->online && recipient->clientSocket > 0) {
        std::string notification = R"({"messageType":"RECEIVE_MESSAGE","messageId":")" + msg.messageId +
                                   R"(","timestamp":)" + std::to_string(msg.timestamp) +
                                   R"(,"payload":{"messageId":")" + msg.messageId +
                                   R"(","senderId":")" + senderId +
                                   R"(","senderName":")" + escapeJson(senderName) +
                                   R"(","messageContent":")" + escapeJson(messageContent) +
                                   R"(","sentAt":)" + std::to_string(msg.timestamp) + R"(}})";

        uint32_t len = htonl(notification.length());
        if (send(recipient->clientSocket, &len, sizeof(len), 0) > 0) {
            if (send(recipient->clientSocket, notification.c_str(), notification.length(), 0) > 0) {
                delivered = true;
                logMessage("SEND", "Client:" + std::to_string(recipient->clientSocket), notification);
            }
        }
    }

    return R"({"messageType":"SEND_MESSAGE_RESPONSE","messageId":")" + messageId +
           R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
           R"(,"payload":{"status":"success","data":{"messageId":")" + msg.messageId +
           R"(","recipientId":")" + recipientId +
           R"(","messageContent":")" + escapeJson(messageContent) +
           R"(","sentAt":)" + std::to_string(msg.timestamp) +
           R"(,"delivered":)" + (delivered ? "true" : "false") + R"(}}})";
}

// Xử lý GET_CHAT_HISTORY_REQUEST
std::string handleGetChatHistory(const std::string& json) {
    std::string payload = getJsonObject(json, "payload");
    std::string messageId = getJsonValue(json, "messageId");
    std::string sessionToken = getJsonValue(json, "sessionToken");
    std::string recipientId = getJsonValue(payload, "recipientId");

    std::string userId = validateSession(sessionToken);
    if (userId.empty()) {
        return R"({"messageType":"GET_CHAT_HISTORY_RESPONSE","messageId":")" + messageId +
               R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
               R"(,"payload":{"status":"error","message":"Invalid or expired session"}})";
    }

    std::stringstream messagesJson;
    messagesJson << "[";
    bool first = true;

    {
        std::lock_guard<std::mutex> lock(chatMutex);
        for (const auto& msg : chatMessages) {
            if ((msg.senderId == userId && msg.recipientId == recipientId) ||
                (msg.senderId == recipientId && msg.recipientId == userId)) {
                if (!first) messagesJson << ",";
                first = false;

                messagesJson << R"({"messageId":")" << msg.messageId
                             << R"(","senderId":")" << msg.senderId
                             << R"(","content":")" << escapeJson(msg.content)
                             << R"(","timestamp":)" << msg.timestamp << "}";
            }
        }
    }
    messagesJson << "]";

    return R"({"messageType":"GET_CHAT_HISTORY_RESPONSE","messageId":")" + messageId +
           R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
           R"(,"payload":{"status":"success","data":{"messages":)" + messagesJson.str() + R"(}}})";
}

// Xử lý SET_LEVEL_REQUEST
std::string handleSetLevel(const std::string& json) {
    std::string payload = getJsonObject(json, "payload");
    std::string messageId = getJsonValue(json, "messageId");
    std::string sessionToken = getJsonValue(json, "sessionToken");
    std::string level = getJsonValue(payload, "level");

    std::string userId = validateSession(sessionToken);
    if (userId.empty()) {
        return R"({"messageType":"SET_LEVEL_RESPONSE","messageId":")" + messageId +
               R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
               R"(,"payload":{"status":"error","message":"Invalid or expired session"}})";
    }

    if (level != "beginner" && level != "intermediate" && level != "advanced") {
        return R"({"messageType":"SET_LEVEL_RESPONSE","messageId":")" + messageId +
               R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
               R"(,"payload":{"status":"error","message":"Invalid level"}})";
    }

    {
        std::lock_guard<std::mutex> lock(usersMutex);
        auto it = userById.find(userId);
        if (it != userById.end()) {
            it->second->level = level;
        }
    }

    return R"({"messageType":"SET_LEVEL_RESPONSE","messageId":")" + messageId +
           R"(","timestamp":)" + std::to_string(getCurrentTimestamp()) +
           R"(,"payload":{"status":"success","message":"Level updated successfully","data":{"level":")" + level + R"("}}})";
}

// ============================================================================
// XỬ LÝ CLIENT
// ============================================================================

void handleClient(int clientSocket, struct sockaddr_in clientAddr) {
    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
    std::string clientInfo = std::string(clientIP) + ":" + std::to_string(ntohs(clientAddr.sin_port));

    std::cout << "[INFO] New connection from " << clientInfo << std::endl;

    char buffer[BUFFER_SIZE];

    while (running) {
        uint32_t msgLen = 0;
        ssize_t bytesRead = recv(clientSocket, &msgLen, sizeof(msgLen), MSG_WAITALL);

        if (bytesRead <= 0) {
            std::cout << "[INFO] Client " << clientInfo << " disconnected" << std::endl;
            break;
        }

        msgLen = ntohl(msgLen);
        if (msgLen > BUFFER_SIZE - 1) {
            std::cout << "[ERROR] Message too long from " << clientInfo << std::endl;
            continue;
        }

        memset(buffer, 0, BUFFER_SIZE);
        bytesRead = recv(clientSocket, buffer, msgLen, MSG_WAITALL);

        if (bytesRead <= 0) {
            std::cout << "[INFO] Client " << clientInfo << " disconnected" << std::endl;
            break;
        }

        std::string message(buffer, bytesRead);
        logMessage("RECV", clientInfo, message);

        std::string messageType = getJsonValue(message, "messageType");
        std::string response;

        if (messageType == "REGISTER_REQUEST") {
            response = handleRegister(message);
        }
        else if (messageType == "LOGIN_REQUEST") {
            response = handleLogin(message, clientSocket);
        }
        else if (messageType == "GET_LESSONS_REQUEST") {
            response = handleGetLessons(message);
        }
        else if (messageType == "GET_LESSON_DETAIL_REQUEST") {
            response = handleGetLessonDetail(message);
        }
        else if (messageType == "GET_TEST_REQUEST") {
            response = handleGetTest(message);
        }
        else if (messageType == "SUBMIT_TEST_REQUEST") {
            response = handleSubmitTest(message);
        }
        else if (messageType == "GET_CONTACT_LIST_REQUEST") {
            response = handleGetContactList(message);
        }
        else if (messageType == "SEND_MESSAGE_REQUEST") {
            response = handleSendMessage(message, clientSocket);
        }
        else if (messageType == "GET_CHAT_HISTORY_REQUEST") {
            response = handleGetChatHistory(message);
        }
        else if (messageType == "SET_LEVEL_REQUEST") {
            response = handleSetLevel(message);
        }
        else {
            response = R"({"messageType":"ERROR_RESPONSE","timestamp":)" +
                       std::to_string(getCurrentTimestamp()) +
                       R"(,"payload":{"status":"error","message":"Unknown message type"}})";
        }

        uint32_t respLen = htonl(response.length());
        send(clientSocket, &respLen, sizeof(respLen), 0);
        send(clientSocket, response.c_str(), response.length(), 0);

        logMessage("SEND", clientInfo, response);
    }

    // Cleanup
    {
        std::lock_guard<std::mutex> lock(sessionsMutex);
        auto it = clientSessions.find(clientSocket);
        if (it != clientSessions.end()) {
            std::string token = it->second;
            auto sessionIt = sessions.find(token);
            if (sessionIt != sessions.end()) {
                std::string uid = sessionIt->second.userId;
                std::lock_guard<std::mutex> userLock(usersMutex);
                auto userIt = userById.find(uid);
                if (userIt != userById.end()) {
                    userIt->second->online = false;
                    userIt->second->clientSocket = -1;
                }
            }
            clientSessions.erase(it);
        }
    }

    close(clientSocket);
}

// ============================================================================
// SIGNAL HANDLER & MAIN
// ============================================================================

void signalHandler(int signal) {
    std::cout << "\n[INFO] Shutting down server..." << std::endl;
    running = false;
    if (serverSocket >= 0) {
        close(serverSocket);
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    int port = DEFAULT_PORT;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGPIPE, SIG_IGN);

    initSampleData();

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        std::cerr << "[ERROR] Cannot create socket" << std::endl;
        return 1;
    }

    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "[ERROR] Cannot bind to port " << port << std::endl;
        close(serverSocket);
        return 1;
    }

    if (listen(serverSocket, MAX_CLIENTS) < 0) {
        std::cerr << "[ERROR] Listen failed" << std::endl;
        close(serverSocket);
        return 1;
    }

    std::cout << "============================================" << std::endl;
    std::cout << "   ENGLISH LEARNING APP - SERVER" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "[INFO] Server started on port " << port << std::endl;
    std::cout << "[INFO] Press Ctrl+C to stop" << std::endl;
    std::cout << "--------------------------------------------" << std::endl;
    std::cout << "Sample accounts:" << std::endl;
    std::cout << "  - student@example.com / student123" << std::endl;
    std::cout << "  - student2@example.com / student123" << std::endl;
    std::cout << "  - sarah@example.com / teacher123" << std::endl;
    std::cout << "--------------------------------------------" << std::endl;
    std::cout << "Lessons: " << lessons.size() << " | Tests: " << tests.size() << std::endl;
    std::cout << "--------------------------------------------" << std::endl;

    while (running) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);

        int clientSock = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientLen);

        if (clientSock < 0) {
            if (running) std::cerr << "[ERROR] Accept failed" << std::endl;
            continue;
        }

        std::thread clientThread(handleClient, clientSock, clientAddr);
        clientThread.detach();
    }

    close(serverSocket);
    return 0;
}

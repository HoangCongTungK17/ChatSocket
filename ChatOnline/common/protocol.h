#ifndef PROTOCOL_H
#define PROTOCOL_H

// Kích thước đệm mặc định cho tin nhắn
#define BUFF_SIZE 2048
#define MAX_USERNAME 64
#define MAX_PASSWORD 64

// Các mã lệnh (Request Type)
typedef enum
{
    REQ_LOGIN = 1,  // Đăng nhập
    REQ_SIGNUP = 2, // Đăng ký
    REQ_LOGOUT = 3, // Đăng xuất

    REQ_LIST_FRIENDS = 4,  // Xem danh sách bạn bè (online/offline)
    REQ_FRIEND_REQ = 5,    // Gửi lời mời kết bạn
    REQ_FRIEND_ACCEPT = 6, // Chấp nhận kết bạn

    REQ_CHAT_PRIVATE = 7, // Chat riêng 1-1
    REQ_CHAT_ROOM = 8,    // Chat trong nhóm

    REQ_CREATE_ROOM = 9,  // Tạo phòng
    REQ_INVITE_ROOM = 10, // Mời vào phòng
    REQ_JOIN_ROOM = 11,   // Chấp nhận vào phòng
    REQ_LEAVE_ROOM = 12,  // Rời phòng

    REQ_VIEW_HISTORY = 13, // Xem lịch sử chat

    RES_SUCCESS = 100, // Phản hồi: Thành công
    RES_ERROR = 101,   // Phản hồi: Lỗi
    RES_DATA = 102     // Phản hồi: Dữ liệu (ví dụ danh sách bạn)
} MessageType;

// Cấu trúc gói tin gửi qua mạng
// Dạng chuỗi: "TYPE|PAYLOAD"
// Ví dụ: "1|username|password" (Login)
// Ví dụ: "7|user_nhan|Noi dung tin nhan" (Chat private)

#endif
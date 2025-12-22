#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>

#include "../common/protocol.h"
#include "libs/user_manager.h"

#define PORT 5500
#define LOG_FILE "server/log.txt"
#define CHAT_DATA_DIR "server/data/chat_data"
#define MAX_CLIENTS 100
#define MAX_ROOMS 10
#define MAX_ROOM_MEMBERS 20

// --- CẤU TRÚC DỮ LIỆU ---
void get_user_joined_rooms_server(const char *username, char *output);
typedef struct
{
    int socket;
    char username[MAX_USERNAME];
} OnlineUser;

typedef struct
{
    char name[64];
    char members[MAX_ROOM_MEMBERS][MAX_USERNAME]; // Lưu tên thành viên
    int count;
} ChatRoom;

// --- BIẾN TOÀN CỤC ---
OnlineUser online_users[MAX_CLIENTS];
ChatRoom chat_rooms[MAX_ROOMS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rooms_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// --- HÀM LOGGING ---
void log_message(const char *message)
{
    pthread_mutex_lock(&log_mutex);
    FILE *fp = fopen(LOG_FILE, "a");
    if (fp)
    {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);
        fprintf(fp, "[%s] %s\n", time_str, message);
        // printf("[%s] %s\n", time_str, message); // Uncomment nếu muốn in log hệ thống
        fclose(fp);
    }
    else
    {
        // In lỗi ra màn hình nếu không mở được file
        printf("\033[0;31m[ERROR] Khong the mo file log tai duong dan: %s\033[0m\n", LOG_FILE);
        perror("Ly do loi"); // In chi tiết lỗi hệ thống (Permission denied, No such file...)
    }
    pthread_mutex_unlock(&log_mutex);
}

void send_packet(int socket, const char *packet)
{
    // printf("  [>>> SENT to sock %d]: %s\n", socket, packet);
    printf("%s\n", packet);

    // char log_buf[BUFF_SIZE + 64];
    // sprintf(log_buf, "SENT to sock %d: %s", socket, packet);
    // log_message(log_buf);

    log_message(packet);

    send(socket, packet, strlen(packet), 0);
}

// --- QUẢN LÝ USER ONLINE ---
void add_online_user(int socket, const char *username)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (online_users[i].socket == 0)
        {
            online_users[i].socket = socket;
            strcpy(online_users[i].username, username);
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void remove_online_user(int socket)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (online_users[i].socket == socket)
        {
            online_users[i].socket = 0;
            online_users[i].username[0] = '\0';
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

int get_socket_by_username(const char *username)
{
    int sock = -1;
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (online_users[i].socket != 0 && strcmp(online_users[i].username, username) == 0)
        {
            sock = online_users[i].socket;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return sock;
}

// --- QUẢN LÝ PHÒNG CHAT (ROOM) ---

// Tìm phòng theo tên
int find_room_index(const char *name)
{
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (strcmp(chat_rooms[i].name, name) == 0)
            return i;
    }
    return -1;
}

//  Thêm tham số owner_name
int create_room_server(const char *room_name, const char *owner_name)
{
    pthread_mutex_lock(&rooms_mutex);
    if (find_room_index(room_name) != -1)
    {
        pthread_mutex_unlock(&rooms_mutex);
        return 0; // Đã tồn tại
    }
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (strlen(chat_rooms[i].name) == 0)
        { // Slot trống
            strcpy(chat_rooms[i].name, room_name);

            //  Thêm ngay người tạo vào slot đầu tiên
            strcpy(chat_rooms[i].members[0], owner_name);
            chat_rooms[i].count = 1; // Khởi tạo số lượng là 1

            pthread_mutex_unlock(&rooms_mutex);
            return 1; // Thành công
        }
    }
    pthread_mutex_unlock(&rooms_mutex);
    return -1; // Full phòng
}
// [Trong file server/libs/room_manager.c hoặc tương tự]
int join_room_server(const char *room_name, const char *username)
{
    char path[1024];
    // HOÀNG TÙNG SỬA: Khớp với đường dẫn file thực tế của bạn
    snprintf(path, sizeof(path), "server/data/chat_data/ROOM_%s.txt", room_name);

    // 1. Kiểm tra sự tồn tại của file phòng trước (Source of Truth)
    if (access(path, F_OK) == -1)
    {
        return -1; // Room not found (File không tồn tại)
    }

    pthread_mutex_lock(&rooms_mutex);

    // 2. Tìm index trong mảng RAM
    int idx = find_room_index(room_name);

    // 3. Nếu file có nhưng RAM chưa có (do Server mới khởi động lại)
    // Ta phải "kích hoạt" phòng này vào RAM
    if (idx == -1)
    {
        // Giả sử bạn có hàm tạo/thêm phòng vào mảng chat_rooms
        // Nếu chưa có, ta tìm một slot trống trong mảng chat_rooms
        for (int i = 0; i < MAX_ROOMS; i++)
        {
            if (strlen(chat_rooms[i].name) == 0)
            {
                strcpy(chat_rooms[i].name, room_name);
                chat_rooms[i].count = 0;
                idx = i;
                break;
            }
        }
    }

    // Nếu vẫn không tìm được slot trống trong RAM
    if (idx == -1)
    {
        pthread_mutex_unlock(&rooms_mutex);
        return 0; // Server quá tải phòng
    }

    // 4. Kiểm tra xem user đã có trong danh sách thành viên (RAM) chưa
    for (int i = 0; i < chat_rooms[idx].count; i++)
    {
        if (strcmp(chat_rooms[idx].members[i], username) == 0)
        {
            pthread_mutex_unlock(&rooms_mutex);
            return 2; // Đã tham gia rồi (thành công nhưng báo hiệu đã có mặt)
        }
    }

    // 5. Thêm user vào danh sách thành viên trong RAM
    if (chat_rooms[idx].count < MAX_ROOM_MEMBERS)
    {
        strncpy(chat_rooms[idx].members[chat_rooms[idx].count], username, MAX_USERNAME - 1);
        chat_rooms[idx].count++;
        pthread_mutex_unlock(&rooms_mutex);

        // (Tùy chọn) Ghi thêm username vào file ROOM_...txt để lưu vĩnh viễn
        // save_member_to_room_file(path, username);

        return 1; // Tham gia mới thành công
    }

    pthread_mutex_unlock(&rooms_mutex);
    return 0; // Phòng đầy
}

// Hàm xóa thành viên khỏi phòng
int leave_room_server(const char *room_name, const char *username)
{
    pthread_mutex_lock(&rooms_mutex);
    int idx = find_room_index(room_name);
    if (idx == -1)
    {
        pthread_mutex_unlock(&rooms_mutex);
        return -1; // Không tìm thấy phòng
    }

    int found = 0;
    for (int i = 0; i < chat_rooms[idx].count; i++)
    {
        if (strcmp(chat_rooms[idx].members[i], username) == 0)
        {
            found = 1;
            // Xóa bằng cách dịch chuyển các phần tử phía sau lên
            for (int j = i; j < chat_rooms[idx].count - 1; j++)
            {
                strcpy(chat_rooms[idx].members[j], chat_rooms[idx].members[j + 1]);
            }
            chat_rooms[idx].count--;
            break;
        }
    }
    pthread_mutex_unlock(&rooms_mutex);
    return found ? 1 : 0; // 1: Thành công, 0: Không có trong phòng
}

// [BỔ SUNG] Hàm dọn dẹp user khỏi tất cả các phòng khi ngắt kết nối
void remove_user_from_all_rooms(const char *username)
{
    pthread_mutex_lock(&rooms_mutex);
    for (int idx = 0; idx < MAX_ROOMS; idx++)
    {
        if (strlen(chat_rooms[idx].name) > 0)
        {
            for (int i = 0; i < chat_rooms[idx].count; i++)
            {
                if (strcmp(chat_rooms[idx].members[i], username) == 0)
                {
                    // Xóa user khỏi phòng này
                    for (int j = i; j < chat_rooms[idx].count - 1; j++)
                    {
                        strcpy(chat_rooms[idx].members[j], chat_rooms[idx].members[j + 1]);
                    }
                    chat_rooms[idx].count--;
                    break;
                }
            }
        }
    }
    pthread_mutex_unlock(&rooms_mutex);
}

// [BỔ SUNG] Hàm lưu lịch sử chat nhóm
void save_room_chat(const char *room_name, const char *sender, const char *msg)
{
    char filename[512];
    // Đặt tiền tố "ROOM_" để phân biệt với file chat riêng
    sprintf(filename, "%s/ROOM_%s.txt", CHAT_DATA_DIR, room_name);

    mkdir(CHAT_DATA_DIR, 0700); // Đảm bảo thư mục tồn tại
    FILE *f = fopen(filename, "a");
    if (f)
    {
        time_t now = time(NULL);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(f, "[%s] %s: %s\n", time_str, sender, msg);
        fclose(f);
    }
}

// Xử lý chat nhóm
void handle_group_chat(const char *sender, const char *room_name, const char *msg)
{
    pthread_mutex_lock(&rooms_mutex);
    int idx = find_room_index(room_name);
    if (idx != -1)
    {

        save_room_chat(room_name, sender, msg);

        for (int i = 0; i < chat_rooms[idx].count; i++)
        {
            char *member_name = chat_rooms[idx].members[i];
            if (strcmp(member_name, sender) != 0)
            {
                int dest_sock = get_socket_by_username(member_name);
                if (dest_sock != -1)
                {
                    char packet[BUFF_SIZE];
                    sprintf(packet, "%d|%s|%s: %s", REQ_CHAT_ROOM, room_name, sender, msg);
                    send_packet(dest_sock, packet);
                }
            }
        }
    }
    pthread_mutex_unlock(&rooms_mutex);
}

void save_private_chat(const char *sender, const char *receiver, const char *msg)
{
    char filename[512];
    if (strcmp(sender, receiver) < 0)
        sprintf(filename, "%s/%s_%s.txt", CHAT_DATA_DIR, sender, receiver);
    else
        sprintf(filename, "%s/%s_%s.txt", CHAT_DATA_DIR, receiver, sender);
    mkdir(CHAT_DATA_DIR, 0700);
    FILE *f = fopen(filename, "a");
    if (f)
    {
        time_t now = time(NULL);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(f, "[%s] %s: %s\n", time_str, sender, msg);
        fclose(f);
    }
}

// --- HÀM XỬ LÝ LẤY DANH SÁCH BẠN ---
void handle_list_friends(int sock, const char *username)
{
    char path[512];
    sprintf(path, "server/data/user_data/%s/friend.txt", username);

    FILE *f = fopen(path, "r");
    char response[BUFF_SIZE] = "";

    if (!f)
    {
        sprintf(response, "%d|You have no friends yet.", RES_DATA);
    }
    else
    {
        char line[MAX_USERNAME];
        char status[20];
        char friend_info[128]; // Tăng size

        sprintf(response, "%d|--- FRIEND LIST ---\n", RES_DATA);

        while (fgets(line, sizeof(line), f))
        {
            line[strcspn(line, "\n")] = 0;
            if (strlen(line) == 0)
                continue;

            int friend_sock = get_socket_by_username(line);
            if (friend_sock != -1)
                strcpy(status, "[ONLINE]");
            else
                strcpy(status, "[OFFLINE]");

            // [SỬA ĐỔI] Dùng snprintf và nối chuỗi an toàn
            snprintf(friend_info, sizeof(friend_info), "%s - %s\n", line, status);

            // Kiểm tra tràn buffer trước khi nối
            if (strlen(response) + strlen(friend_info) < BUFF_SIZE - 1)
            {
                strcat(response, friend_info);
            }
            else
            {
                strcat(response, "...(List truncated)...\n");
                break;
            }
        }
        fclose(f);
    }
    send_packet(sock, response);
}

// --- THREAD XỬ LÝ ---
void *connection_handler(void *socket_desc)
{
    int sock = *(int *)socket_desc;
    free(socket_desc);

    char buffer[BUFF_SIZE];
    char response[BUFF_SIZE];
    int read_size;
    int current_user_id = -1;
    char current_username[MAX_USERNAME] = "";

    while ((read_size = recv(sock, buffer, BUFF_SIZE - 1, 0)) > 0)
    {
        buffer[read_size] = '\0';
        // printf("[<<< RECV from sock %d]: %s\n", sock, buffer);
        printf("%s\n", buffer);

        // char log_buf[BUFF_SIZE + 64];
        // sprintf(log_buf, "RECV from sock %d: %s", sock, buffer);
        // log_message(log_buf);
        log_message(buffer);

        char temp_buf[BUFF_SIZE];
        strcpy(temp_buf, buffer);
        char *type_str = strtok(temp_buf, "|");
        if (!type_str)
            continue;
        int type = atoi(type_str);

        switch (type)
        {
        case REQ_LOGIN:
        {
            char *user = strtok(NULL, "|");
            char *pass = strtok(NULL, "|");
            if (user && pass)
            {
                int id = check_login(user, pass);
                if (id != -1)
                {
                    current_user_id = id;
                    strcpy(current_username, user);
                    add_online_user(sock, current_username);
                    sprintf(response, "%d|Login successful!", RES_SUCCESS);
                }
                else
                    sprintf(response, "%d|Invalid login", RES_ERROR);
            }
            else
                sprintf(response, "%d|Missing params", RES_ERROR);
            send_packet(sock, response);
            break;
        }
        case REQ_SIGNUP:
        {
            char *user = strtok(NULL, "|");
            char *pass = strtok(NULL, "|");
            if (user && pass)
            {
                if (register_user(user, pass))
                    sprintf(response, "%d|Signup successful!", RES_SUCCESS);
                else
                    sprintf(response, "%d|User exists", RES_ERROR);
            }
            send_packet(sock, response);
            break;
        }
        case REQ_CHAT_PRIVATE:
        {
            char *recipient = strtok(NULL, "|");
            char *msg_content = strtok(NULL, "|");
            if (current_user_id != -1 && recipient && msg_content)
            {
                save_private_chat(current_username, recipient, msg_content);
                int dest_sock = get_socket_by_username(recipient);
                if (dest_sock != -1)
                {
                    char fwd[BUFF_SIZE];
                    sprintf(fwd, "%d|%s|%s", REQ_CHAT_PRIVATE, current_username, msg_content);
                    send_packet(dest_sock, fwd);
                }
                else
                {
                    sprintf(response, "%d|User offline. Saved.", RES_DATA);
                    send_packet(sock, response);
                }
            }
            break;
        }
        case REQ_CREATE_ROOM:
        {
            char *room_name = strtok(NULL, "|");
            if (current_user_id != -1 && room_name)
            {
                int res = create_room_server(room_name, current_username);
                if (res == 1)
                    sprintf(response, "%d|Room '%s' created.", RES_SUCCESS, room_name);
                else
                    sprintf(response, "%d|Room exists or full.", RES_ERROR);
            }
            else
                sprintf(response, "%d|Error.", RES_ERROR);
            send_packet(sock, response);
            break;
        }
        case REQ_CHAT_ROOM:
        {
            char *room_name = strtok(NULL, "|");
            char *msg = strtok(NULL, "|");
            if (current_user_id != -1 && room_name && msg)
            {
                handle_group_chat(current_username, room_name, msg);
            }
            break;
        }
        case REQ_LIST_FRIENDS:
        {
            if (current_user_id != -1)
                handle_list_friends(sock, current_username);
            break;
        }
        case REQ_FRIEND_REQ:
        {
            char *target = strtok(NULL, "|");
            if (current_user_id != -1 && target)
            {
                if (strcmp(target, current_username) == 0)
                {
                    sprintf(response, "%d|Cannot add yourself.", RES_ERROR);
                }
                else
                {
                    int res = send_friend_request(current_username, target);
                    if (res == 1)
                    {
                        sprintf(response, "%d|Request sent to %s", RES_SUCCESS, target);

                        // GỬI THÔNG BÁO REAL-TIME CHO NGƯỜI NHẬN ---
                        // 1. Tìm socket của người nhận (target)
                        int target_sock = get_socket_by_username(target);

                        // 2. Nếu tìm thấy socket (nghĩa là người nhận đang Online)
                        if (target_sock != -1)
                        {
                            char notify[BUFF_SIZE];
                            // Gói tin gửi cho người nhận: "MÃ_YÊU_CẦU | TÊN_NGƯỜI_GỬI"
                            // Ví dụ: "6|tung"
                            sprintf(notify, "%d|%s", REQ_FRIEND_REQ, current_username);

                            // Gửi thông báo đến máy người nhận
                            send_packet(target_sock, notify);
                        }
                        // ------------------------------------------------------------
                    }
                    else if (res == 2)
                        sprintf(response, "%d|Already friends or requested", RES_ERROR);
                    else
                        sprintf(response, "%d|User not found", RES_ERROR);
                }
            }
            else
                sprintf(response, "%d|Error params", RES_ERROR);

            // Gửi phản hồi về cho người gửi (Tùng) để báo "Đã gửi thành công"
            send_packet(sock, response);
            break;
        }
        case REQ_FRIEND_ACCEPT:
        {
            char *target = strtok(NULL, "|");
            if (target == NULL)
            {
                char list_buf[BUFF_SIZE];
                get_friend_requests(current_username, list_buf);
                // Hướng dẫn người dùng thao tác trên Menu
                snprintf(response, BUFF_SIZE, "%d|Danh sach loi moi: %s\n(HD: Chon lai muc 7 va nhap ten de chap nhan)", RES_DATA, list_buf);
            }
            else
            {
                if (accept_friend_request(current_username, target))
                {
                    sprintf(response, "%d|You are now friends with %s", RES_SUCCESS, target);
                }
                else
                {
                    sprintf(response, "%d|Request not found or failed", RES_ERROR);
                }
            }
            send_packet(sock, response);
            break;
        }
        case REQ_INVITE_ROOM:
        {
            char *target_user = strtok(NULL, "|");
            char *room_name = strtok(NULL, "|");

            if (target_user && room_name)
            {
                int dest_sock = get_socket_by_username(target_user);
                if (dest_sock != -1)
                {
                    char invite_msg[BUFF_SIZE];
                    sprintf(invite_msg, "%d|%s|%s", REQ_INVITE_ROOM, current_username, room_name);
                    send_packet(dest_sock, invite_msg);
                    sprintf(response, "%d|Invitation sent to %s", RES_SUCCESS, target_user);
                }
                else
                {
                    sprintf(response, "%d|User %s is offline", RES_ERROR, target_user);
                }
            }
            else
                sprintf(response, "%d|Invalid format", RES_ERROR);
            send_packet(sock, response);
            break;
        }
        case REQ_JOIN_ROOM:
        {
            char *room_name = strtok(NULL, "|");
            if (current_user_id != -1 && room_name)
            {
                // --- KIỂM TRA XEM LÀ LỆNH LẤY DANH SÁCH HAY LÀ VÀO PHÒNG ---
                if (strcmp(room_name, "LIST") == 0)
                {
                    // Trường hợp người dùng lỡ quên tên, muốn xem danh sách phòng đã vào
                    char room_list[BUFF_SIZE] = "";

                    // Bạn cần viết hàm này để quét danh sách các phòng mà user đang tham gia
                    get_user_joined_rooms_server(current_username, room_list);

                    // Trả về mã RES_DATA (102) kèm danh sách phòng
                    sprintf(response, "%d|Danh sach phong: %s", RES_DATA, room_list);
                }
                else
                {
                    // ---  VÀO PHÒNG THẬT SỰ ---
                    int res = join_room_server(room_name, current_username);
                    if (res == 1)
                        sprintf(response, "%d|Joined room '%s' successfully", RES_SUCCESS, room_name);
                    else if (res == 2)
                        sprintf(response, "%d|You are already in room '%s'", RES_SUCCESS, room_name);
                    else
                        sprintf(response, "%d|Room not found", RES_ERROR);
                }
            }
            else
            {
                sprintf(response, "%d|Error params", RES_ERROR);
            }

            // Gửi gói tin (danh sách phòng hoặc kết quả join) về cho Client
            send_packet(sock, response);
            break;
        }
        case REQ_LEAVE_ROOM:
        {
            char *room_name = strtok(NULL, "|");
            if (current_user_id != -1 && room_name)
            {
                if (leave_room_server(room_name, current_username))
                    sprintf(response, "%d|Left room '%s'", RES_SUCCESS, room_name);
                else
                    sprintf(response, "%d|You are not in room '%s'", RES_ERROR, room_name);
            }
            send_packet(sock, response);
            break;
        }
        case REQ_VIEW_HISTORY:
        { // 13|target (có thể là username hoặc tên phòng)
            char *target = strtok(NULL, "|");
            if (current_user_id != -1 && target)
            {
                char filename[512];
                FILE *f = NULL;

                // 1. Kiểm tra xem target có phải là tên phòng không
                pthread_mutex_lock(&rooms_mutex);
                int is_room = (find_room_index(target) != -1);
                pthread_mutex_unlock(&rooms_mutex);

                if (is_room)
                {
                    // Nếu là phòng, đọc file ROOM_tenphong.txt
                    sprintf(filename, "%s/ROOM_%s.txt", CHAT_DATA_DIR, target);
                }
                else
                {
                    // Nếu là người, đọc file userA_userB.txt (như cũ)
                    if (strcmp(current_username, target) < 0)
                        sprintf(filename, "%s/%s_%s.txt", CHAT_DATA_DIR, current_username, target);
                    else
                        sprintf(filename, "%s/%s_%s.txt", CHAT_DATA_DIR, target, current_username);
                }

                f = fopen(filename, "r");
                if (f)
                {
                    char history_data[BUFF_SIZE * 2] = "";
                    char line[256];

                    if (is_room)
                        sprintf(history_data, "%d|--- GROUP HISTORY: %s ---\n", RES_DATA, target);
                    else
                        sprintf(history_data, "%d|--- PRIVATE HISTORY: %s ---\n", RES_DATA, target);

                    while (fgets(line, sizeof(line), f))
                    {
                        if (strlen(history_data) + strlen(line) < BUFF_SIZE * 2 - 100)
                            strcat(history_data, line);
                        else
                        {
                            strcat(history_data, "...(More)...\n");
                            break;
                        }
                    }
                    fclose(f);
                    send_packet(sock, history_data);
                }
                else
                {
                    sprintf(response, "%d|No history found with %s", RES_ERROR, target);
                    send_packet(sock, response);
                }
            }
            break;
        }
        case REQ_LOGOUT:
        {
            remove_online_user(sock);
            remove_user_from_all_rooms(current_username); // [BỔ SUNG] Dọn dẹp phòng
            current_user_id = -1;
            break;
        }
        default:
            break;
        }
        memset(buffer, 0, BUFF_SIZE);
    }

    // Khi ngắt kết nối (read_size <= 0)
    if (current_user_id != -1)
    {
        remove_online_user(sock);
        remove_user_from_all_rooms(current_username); // [BỔ SUNG]
        printf("User %s disconnected.\n", current_username);

        char log_buf[200];
        sprintf(log_buf, "User '%s' da ngat ket noi.", current_username);
        log_message(log_buf);
    }

    close(sock);
    return 0;
}

int main()
{
    int server_sock, *new_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size;

    memset(online_users, 0, sizeof(online_users));
    for (int i = 0; i < MAX_ROOMS; i++)
        chat_rooms[i].count = 0;

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        return 1;
    }

    listen(server_sock, 10);
    printf("SERVER STARTED ON PORT %d\n", PORT);
    log_message("Server started on port 5500...");
    mkdir("server/data", 0700);
    mkdir(CHAT_DATA_DIR, 0700);

    while (1)
    {
        addr_size = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_size);
        if (client_sock < 0)
            continue;

        char log_conn[100];
        sprintf(log_conn, "Co ket noi moi tu socket: %d", client_sock);
        log_message(log_conn);

        pthread_t sniffer_thread;
        new_sock = malloc(sizeof(int));
        *new_sock = client_sock;
        if (pthread_create(&sniffer_thread, NULL, connection_handler, (void *)new_sock) < 0)
            return 1;
        pthread_detach(sniffer_thread);
    }
    return 0;
}
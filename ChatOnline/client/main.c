#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#include "../common/protocol.h"

#define SERVER_IP "127.0.0.1"
#define PORT 5500

// --- CÁC TRẠNG THÁI CỦA GIAO DIỆN ---
typedef enum
{
    STATE_LOGIN_MENU,   // Menu đăng nhập/đăng ký
    STATE_MAIN_MENU,    // Menu chính sau khi login
    STATE_CHAT_PRIVATE, // Đang chat riêng 1-1
    STATE_CHAT_ROOM     // Đang chat trong phòng
} AppState;

// --- BIẾN TOÀN CỤC ---
int sock;
volatile int is_running = 1;
volatile AppState current_state = STATE_LOGIN_MENU;

// Biến lưu thông tin phiên làm việc
char my_username[MAX_USERNAME];
char target_name[MAX_USERNAME]; // Lưu tên người/phòng đang chat

// --- HÀM HỖ TRỢ GIAO DIỆN ---
void clear_screen()
{
    // Lệnh xóa màn hình (Linux/Mac dùng "clear", Windows dùng "cls")
    system("clear");
}

void print_banner()
{
    printf("\n==========================================\n");
    printf("        C-SOCKET CHAT TERMINAL v2.0       \n");
    printf("==========================================\n");
}

void show_login_menu()
{
    // clear_screen();
    print_banner();
    printf("1. Dang nhap (Login)\n");
    printf("2. Dang ky (Register)\n");
    printf("3. Thoat (Exit)\n");
    printf(">>> Chon: ");
}

void show_main_menu()
{
    print_banner();
    printf("Xin chao, %s!\n", my_username);
    printf("------------------------------------------\n");
    printf("1. Chat rieng (Private Chat)\n");
    printf("2. Tao phong chat (Create Room)\n");
    printf("3. Vao phong chat (Join Room)\n");
    printf("4. Moi ban vao phong (Invite)\n"); // [MỚI]
    printf("5. Danh sach ban be (Friend List)\n");
    printf("6. Them ban (Add Friend)\n");
    printf("7. Chap nhan ket ban (Accept Friend)\n");
    printf("8. Lich su chat\n");
    printf("9. Dang xuat\n");
    printf(">>> Chon: ");
}

// --- LUỒNG NHẬN TIN NHẮN (RECEIVE THREAD) ---
void *recv_msg_handler(void *socket_desc)
{
    int server_sock = *(int *)socket_desc;
    char buffer[BUFF_SIZE];
    int len;

    while (is_running)
    {
        memset(buffer, 0, BUFF_SIZE);
        len = recv(server_sock, buffer, BUFF_SIZE - 1, 0);
        if (len > 0)
        {
            buffer[len] = '\0';

            // Phân tích gói tin
            char temp[BUFF_SIZE];
            strcpy(temp, buffer);
            char *type_str = strtok(temp, "|");
            int type = atoi(type_str);
            char *param1 = strtok(NULL, "|");
            char *param2 = strtok(NULL, "");

            // Xử lý hiển thị dựa trên trạng thái hiện tại
            if (type == RES_SUCCESS || type == RES_ERROR)
            {
                printf("\n[THONG BAO]: %s\n", param1 ? param1 : "Success");
                if (current_state == STATE_LOGIN_MENU || current_state == STATE_MAIN_MENU)
                {
                    printf(">>> Nhan Enter de tiep tuc...");
                    fflush(stdout);
                }
            }
            else if (type == REQ_INVITE_ROOM)
            {
                // Server gửi: 10|inviter|room_name
                // param1 là inviter, param2 là room_name
                printf("\n>>> [THONG BAO] Ban nhan duoc loi moi vao phong '%s' tu '%s'.\n", param2, param1);
                printf(">>> Go lenh 'Join Room' (Menu 3) va nhap '%s' de tham gia.\n", param2);
                printf("YOU: ");
                fflush(stdout);
            }
            else if (type == REQ_CHAT_PRIVATE)
            {
                // Nếu đang chat với đúng người đó thì in ra đẹp
                if (current_state == STATE_CHAT_PRIVATE && strcmp(target_name, param1) == 0)
                {
                    printf("\r\033[K"); // Xóa dòng hiện tại
                    printf("[P] %s: %s\n", param1, param2);
                    printf("YOU: ");
                    fflush(stdout);
                }
                else
                {
                    printf("\n[Tin nhan moi tu %s]: %s\n", param1, param2);
                    if (current_state != STATE_CHAT_PRIVATE)
                        printf(">>> ");
                    fflush(stdout);
                }
            }
            else if (type == REQ_CHAT_ROOM)
            {
                // param1: room_name, param2: sender: message
                if (current_state == STATE_CHAT_ROOM && strcmp(target_name, param1) == 0)
                {
                    printf("\r\033[K");
                    printf("[R] %s\n", param2); // param2 đã chứa "sender: msg"
                    printf("YOU: ");
                    fflush(stdout);
                }
                else
                {
                    printf("\n[Room %s]: %s\n", param1, param2);
                    if (current_state != STATE_CHAT_ROOM)
                        printf(">>> ");
                    fflush(stdout);
                }
            }
            else if (type == RES_DATA) // [BỔ SUNG] Xử lý hiển thị danh sách bạn/lịch sử
            {
                printf("\n%s\n", param1 ? param1 : "");
                printf(">>> ");
                fflush(stdout);
            }
            else
            {
                printf("\nServer: %s\n", buffer);
            }
        }
        else
        {
            is_running = 0;
            break;
        }
    }
    return NULL;
}

// --- XỬ LÝ GỬI TIN (MAIN THREAD) ---
// Hàm này chứa logic điều hướng menu
void send_msg_handler(int sock)
{
    char message[BUFF_SIZE];
    char buffer[BUFF_SIZE];
    int choice;

    while (is_running)
    {
        // --- 1. MÀN HÌNH LOGIN ---
        if (current_state == STATE_LOGIN_MENU)
        {
            show_login_menu();
            if (scanf("%d", &choice) != 1)
            {
                while (getchar() != '\n')
                    ;
                continue;
            }
            while (getchar() != '\n')
                ; // Xóa bộ nhớ đệm

            char u[MAX_USERNAME], p[MAX_PASSWORD];

            if (choice == 3)
            {
                is_running = 0;
                break;
            }

            printf("Username: ");
            fgets(u, sizeof(u), stdin);
            u[strcspn(u, "\n")] = 0;
            printf("Password: ");
            fgets(p, sizeof(p), stdin);
            p[strcspn(p, "\n")] = 0;

            if (choice == 1)
            { // Login
                sprintf(message, "%d|%s|%s", REQ_LOGIN, u, p);
                send(sock, message, strlen(message), 0);

                // Hack: Tạm thời cho login luôn thành công ở phía client để chuyển menu
                strcpy(my_username, u);
                sleep(1); // Đợi server trả lời
                current_state = STATE_MAIN_MENU;
            }
            else if (choice == 2)
            { // Register
                sprintf(message, "%d|%s|%s", REQ_SIGNUP, u, p);
                send(sock, message, strlen(message), 0);
                sleep(1);
            }
        }

        // --- 2. MÀN HÌNH CHÍNH ---
        else if (current_state == STATE_MAIN_MENU)
        {
            show_main_menu();
            if (scanf("%d", &choice) != 1)
            {
                while (getchar() != '\n')
                    ;
                continue;
            }
            while (getchar() != '\n')
                ;

            if (choice == 1)
            { // Chat Private
                printf("Nhap username nguoi muon chat: ");
                fgets(target_name, sizeof(target_name), stdin);
                target_name[strcspn(target_name, "\n")] = 0;
                current_state = STATE_CHAT_PRIVATE;
            }
            else if (choice == 2)
            { // Create Room
                printf("Nhap ten phong muon tao: ");
                fgets(buffer, sizeof(buffer), stdin);
                buffer[strcspn(buffer, "\n")] = 0;
                sprintf(message, "%d|%s", REQ_CREATE_ROOM, buffer);
                send(sock, message, strlen(message), 0);
                sleep(1);
            }
            else if (choice == 3)
            { // Join Room
                printf("Nhap ten phong muon vao: ");
                fgets(target_name, sizeof(target_name), stdin);
                target_name[strcspn(target_name, "\n")] = 0;

                // Gửi lệnh join
                sprintf(message, "%d|%s", REQ_JOIN_ROOM, target_name);
                send(sock, message, strlen(message), 0);

                // Chuyển sang màn hình chat room
                current_state = STATE_CHAT_ROOM;
                sleep(1);
            }
            // [SỬA ĐỔI] Sắp xếp lại thứ tự các case cho khớp với Menu hiển thị
            else if (choice == 4)
            { // [BỔ SUNG] Invite Friend to Room
                char room[64], friend[64];
                printf("Nhap ten phong ban dang o (hoac muon moi): ");
                fgets(room, 64, stdin);
                room[strcspn(room, "\n")] = 0;
                printf("Nhap ten ban muon moi: ");
                fgets(friend, 64, stdin);
                friend[strcspn(friend, "\n")] = 0;

                // Gửi: 10|friend|room
                sprintf(message, "%d|%s|%s", REQ_INVITE_ROOM, friend, room);
                send(sock, message, strlen(message), 0);
                sleep(1);
            }
            else if (choice == 5)
            { // List Friends (Đẩy xuống số 5)
                sprintf(message, "%d|", REQ_LIST_FRIENDS);
                send(sock, message, strlen(message), 0);
                sleep(1);
            }
            else if (choice == 6)
            { // Add Friend (Đẩy xuống số 6)
                printf("Nhap username muon ket ban: ");
                fgets(target_name, sizeof(target_name), stdin);
                target_name[strcspn(target_name, "\n")] = 0;

                sprintf(message, "%d|%s", REQ_FRIEND_REQ, target_name);
                send(sock, message, strlen(message), 0);
                sleep(1);
            }
            else if (choice == 7)
            { // Accept Friend (Đẩy xuống số 7)
                printf("Nhap username muon chap nhan (Enter de xem danh sach): ");
                fgets(target_name, sizeof(target_name), stdin);
                target_name[strcspn(target_name, "\n")] = 0;

                if (strlen(target_name) == 0)
                {
                    sprintf(message, "%d|", REQ_FRIEND_ACCEPT);
                }
                else
                {
                    sprintf(message, "%d|%s", REQ_FRIEND_ACCEPT, target_name);
                }
                send(sock, message, strlen(message), 0);
                sleep(1);
            }
            else if (choice == 8)
            { // History (Đẩy xuống số 8)
                printf("Nhap username muon xem lich su: ");
                fgets(target_name, sizeof(target_name), stdin);
                target_name[strcspn(target_name, "\n")] = 0;
                sprintf(message, "%d|%s", REQ_VIEW_HISTORY, target_name);
                send(sock, message, strlen(message), 0);
                sleep(1);
            }
            else if (choice == 9)
            { // Logout (Đẩy xuống số 9)
                sprintf(message, "%d|", REQ_LOGOUT);
                send(sock, message, strlen(message), 0);
                current_state = STATE_LOGIN_MENU;
            }
        }

        // --- 3. MÀN HÌNH CHAT (RIÊNG HOẶC ROOM) ---
        else if (current_state == STATE_CHAT_PRIVATE || current_state == STATE_CHAT_ROOM)
        {
            clear_screen();
            if (current_state == STATE_CHAT_ROOM)
            {
                printf("--- ROOM: %s (Go 'LEAVE' de roi phong) ---\n", target_name);
            }
            else
            {
                printf("--- PRIVATE: %s (Go 'EXIT' de quay lai) ---\n", target_name);
            }
            printf("-----------------------------------------\n");

            while (1)
            {
                printf("YOU: ");
                memset(buffer, 0, BUFF_SIZE);
                fgets(buffer, BUFF_SIZE, stdin);
                buffer[strcspn(buffer, "\n")] = 0;

                // Xử lý thoát chat
                if (strcmp(buffer, "EXIT") == 0)
                {
                    current_state = STATE_MAIN_MENU;
                    break;
                }

                // [BỔ SUNG] Xử lý rời phòng chat
                if (current_state == STATE_CHAT_ROOM && strcmp(buffer, "LEAVE") == 0)
                {
                    // Gửi lệnh rời phòng: 12|room_name
                    sprintf(message, "%d|%s", REQ_LEAVE_ROOM, target_name);
                    send(sock, message, strlen(message), 0);

                    current_state = STATE_MAIN_MENU;
                    break;
                }

                if (strlen(buffer) > 0)
                {
                    if (current_state == STATE_CHAT_PRIVATE)
                    {
                        sprintf(message, "%d|%s|%s", REQ_CHAT_PRIVATE, target_name, buffer);
                    }
                    else
                    {
                        sprintf(message, "%d|%s|%s", REQ_CHAT_ROOM, target_name, buffer);
                    }
                    send(sock, message, strlen(message), 0);
                }
            }
        }
    }
}

int main()
{
    struct sockaddr_in server_addr;
    pthread_t recv_thread;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Socket failed");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connect failed");
        return 1;
    }

    if (pthread_create(&recv_thread, NULL, recv_msg_handler, (void *)&sock) < 0)
        return 1;

    send_msg_handler(sock);

    close(sock);
    return 0;
}
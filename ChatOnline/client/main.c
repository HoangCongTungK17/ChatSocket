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
volatile int login_success = 0; // Cờ kiểm tra đăng nhập thành công

// Biến lưu thông tin phiên làm việc
char my_username[MAX_USERNAME];
char target_name[MAX_USERNAME]; // Lưu tên người/phòng đang chat

// --- DANH SÁCH LƯU TẠM ĐỂ CHỌN NHANH ---
#define MAX_PENDING 5

// Lưu tên các phòng vừa được mời
char pending_rooms[MAX_PENDING][64];
int room_invite_count = 0;

char joined_rooms[MAX_PENDING][64];
int joined_room_count = 0;

// Lưu tên các người gửi lời mời kết bạn
char pending_friends[MAX_PENDING][64];
int friend_invite_count = 0;

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
    printf(">>> Chon : ");
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
            if (type == RES_SUCCESS)
            {
                printf("\n[THONG BAO]: %s\n", param1 ? param1 : "Success");

                if (current_state == STATE_LOGIN_MENU)
                {
                    login_success = 1; // Báo hiệu đã đăng nhập thành công
                }

                if (current_state == STATE_MAIN_MENU) // Chỉ hiện "Enter" khi ở menu chính
                {
                    printf(">>> Nhan Enter de tiep tuc...");
                    fflush(stdout);
                }
            }
            else if (type == RES_ERROR)
            {
                printf("\n[LOI]: %s\n", param1 ? param1 : "Error");

                if (current_state == STATE_LOGIN_MENU)
                {
                    login_success = -1; // Báo hiệu đăng nhập thất bại
                }
                // ----------------
            }
            else if (type == REQ_INVITE_ROOM)
            {
                if (room_invite_count < MAX_PENDING)
                {
                    strncpy(pending_rooms[room_invite_count], param2, 63);
                    printf("\n\033[1;33m[MOI PHONG %d]\033[0m %s moi ban vao '%s'.",
                           room_invite_count + 1, param1, param2);
                    printf("\n>>> Go 'Y%d' de vao ngay, hoac tiep tuc menu: ", room_invite_count + 1);
                    room_invite_count++;
                    fflush(stdout);
                }
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
            // Thêm case xử lý thông báo kết bạn đến bất ngờ
            else if (type == REQ_FRIEND_REQ) // Giả sử mã REQ_FRIEND_REQ là 8
            {
                // param1 lúc này là tên người gửi yêu cầu
                printf("\n\033[1;35m[KET BAN]\033[0m %s vua gui loi moi ket ban cho ban!", param1);
                printf("\n>>> Chon muc 7 de xem danh sach va chap nhan.");
                printf("\n>>> ");
                fflush(stdout);
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
            else if (type == RES_DATA && strstr(param1, "Danh sach loi moi"))
            {
                // 1. In tiêu đề danh sách cho đẹp
                printf("\n\033[1;32m--- DANH SÁCH LỜI MỜI KẾT BẠN ---\033[0m\n");

                // 2. Tìm vị trí của danh sách tên (sau dấu ':')
                char *list_part = strchr(param1, ':');
                if (list_part)
                {
                    list_part++;             // Nhảy qua dấu ':'
                    friend_invite_count = 0; // Reset đếm

                    // 3. Tách tên, nhưng dừng lại nếu gặp dòng hướng dẫn (bắt đầu bằng dấu \n hoặc '(')
                    char *name = strtok(list_part, ", \n");
                    while (name && friend_invite_count < MAX_PENDING)
                    {
                        // Nếu chữ tách được bắt đầu bằng dấu ngoặc '(' thì đây là hướng dẫn, dừng lại ngay!
                        if (name[0] == '(')
                            break;

                        // Lưu tên vào ngăn chứa tạm
                        strncpy(pending_friends[friend_invite_count], name, 63);

                        // In ra dòng chọn cực gọn:
                        printf(" [%d] Chap nhan: %-15s -> Go 'A%d'\n",
                               friend_invite_count + 1, name, friend_invite_count + 1);

                        friend_invite_count++;
                        name = strtok(NULL, ", \n");
                    }
                }

                if (friend_invite_count == 0)
                {
                    printf(" (Trong)\n");
                }
                printf("----------------------------------\n>>> ");
                fflush(stdout);
            }

            else if (type == RES_DATA && strstr(param1, "Danh sach phong"))
            {
                printf("\n\033[1;34m--- PHÒNG CỦA BẠN ---\033[0m\n");
                char *list_part = strchr(param1, ':');
                if (list_part)
                {
                    list_part++;
                    joined_room_count = 0;
                    char *name = strtok(list_part, ", \n");
                    while (name && joined_room_count < MAX_PENDING)
                    {
                        strncpy(joined_rooms[joined_room_count], name, 63);
                        printf(" [J%d] %-15s (Bam 'J%d' de vao)\n",
                               joined_room_count + 1, name, joined_room_count + 1);
                        joined_room_count++;
                        name = strtok(NULL, ", \n");
                    }
                }
                if (joined_room_count == 0)
                    printf(" (Ban chua tham gia phong nao)\n");
                printf("---------------------\n");
                fflush(stdout);
            }

            else if (type == RES_DATA) //  Xử lý hiển thị danh sách bạn/lịch sử
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

                login_success = 0; // Reset cờ
                printf("Dang cho server phan hoi...");
                fflush(stdout);

                // Vòng lặp chờ phản hồi (Timeout khoảng 2 giây)
                int timeout = 20;
                while (login_success == 0 && timeout > 0)
                {
                    usleep(100000); // Ngủ 0.1s
                    timeout--;
                }

                if (login_success == 1)
                {
                    strcpy(my_username, u);
                    current_state = STATE_MAIN_MENU;
                }
                else if (login_success == -1)
                {
                    printf("\n>>> Dang nhap that bai! Sai username hoac password.\n");
                    printf(">>> Nhan Enter de quay lai menu login...");
                }
                else
                {
                    printf("\n>>> Server khong phan hoi (Timeout).\n");
                }
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
            char input[64];

            fgets(input, sizeof(input), stdin);

            input[strcspn(input, "\n")] = 0;

            // Kiểm tra lệnh tắt Y (Vào phòng)

            if ((input[0] == 'Y' || input[0] == 'y') && strlen(input) > 1)

            {

                int idx = atoi(&input[1]) - 1;

                if (idx >= 0 && idx < room_invite_count)

                {

                    sprintf(message, "%d|%s", REQ_JOIN_ROOM, pending_rooms[idx]);

                    send(sock, message, strlen(message), 0);

                    strcpy(target_name, pending_rooms[idx]);

                    current_state = STATE_CHAT_ROOM;

                    room_invite_count = 0;

                    continue;
                }
            }

            else if ((input[0] == 'A' || input[0] == 'a') && strlen(input) > 1)

            {

                int idx = atoi(&input[1]) - 1;

                if (idx >= 0 && idx < friend_invite_count)

                {

                    sprintf(message, "%d|%s", REQ_FRIEND_ACCEPT, pending_friends[idx]);

                    send(sock, message, strlen(message), 0);

                    friend_invite_count = 0;

                    sleep(1);

                    continue;
                }
            }

            choice = atoi(input);
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
                // Lưu tên phòng vào biến toàn cục để dùng cho giao diện chat
                strcpy(target_name, buffer);
                sprintf(message, "%d|%s", REQ_CREATE_ROOM, buffer);
                send(sock, message, strlen(message), 0);
                // current_state = STATE_CHAT_ROOM;
                sleep(1);
            }
            else if (choice == 3)
            {
                printf("\n\033[1;36m[HE THONG]\033[0m: Dang tai danh sach phong chat... (Nhan Enter)");
                char wait_enter[10];
                fgets(wait_enter, sizeof(wait_enter), stdin);

                // 1. Gửi lệnh yêu cầu danh sách phòng (Giao thức tùy bạn, ví dụ gửi mã REQ_JOIN_ROOM kèm chuỗi trống)
                sprintf(message, "%d|LIST", REQ_JOIN_ROOM);
                send(sock, message, strlen(message), 0);

                // 2. Đợi luồng Recv in danh sách phòng đã tham gia & danh sách mời
                usleep(500000);

                // 3. Hiển thị lại các lời mời đang chờ (nếu có) để người dùng chọn luôn
                if (room_invite_count > 0)
                {
                    printf("\n\033[1;33m--- LOI MOI DANG CHO ---\033[0m\n");
                    for (int i = 0; i < room_invite_count; i++)
                    {
                        printf(" [Y%d] %-15s (Bam 'Y%d' de chap nhan)\n", i + 1, pending_rooms[i], i + 1);
                    }
                    printf("------------------------\n");
                }

                // 4. Cho phép nhập lựa chọn ngay tại đây
                printf("\n>>> Nhap phim tat (J1, Y1...) hoac ten phong (Go 'B' de quay lai): ");
                fflush(stdout);

                char sub_input[64];
                fgets(sub_input, sizeof(sub_input), stdin);
                sub_input[strcspn(sub_input, "\n")] = 0;

                if (strlen(sub_input) == 0)
                    continue;

                if (strcmp(sub_input, "B") == 0 || strcmp(sub_input, "b") == 0)
                {
                    continue; // Thoát ra menu chính ngay lập tức
                }

                // Xử lý phím tắt J (Phòng đã tham gia)
                if ((sub_input[0] == 'J' || sub_input[0] == 'j') && strlen(sub_input) > 1)
                {
                    int idx = atoi(&sub_input[1]) - 1;
                    if (idx >= 0 && idx < joined_room_count)
                    {
                        strcpy(target_name, joined_rooms[idx]);
                        sprintf(message, "%d|%s", REQ_JOIN_ROOM, target_name);
                        send(sock, message, strlen(message), 0);
                        // current_state = STATE_CHAT_ROOM;
                    }
                }
                // Xử lý phím tắt Y (Lời mời mới)
                else if ((sub_input[0] == 'Y' || sub_input[0] == 'y') && strlen(sub_input) > 1)
                {
                    int idx = atoi(&sub_input[1]) - 1;
                    if (idx >= 0 && idx < room_invite_count)
                    {
                        strcpy(target_name, pending_rooms[idx]);
                        sprintf(message, "%d|%s", REQ_JOIN_ROOM, target_name);
                        send(sock, message, strlen(message), 0);
                        current_state = STATE_CHAT_ROOM;
                    }
                }
                // Nếu họ vẫn muốn nhập tên thủ công
                else
                {
                    strcpy(target_name, sub_input);
                    sprintf(message, "%d|%s", REQ_JOIN_ROOM, target_name);
                    send(sock, message, strlen(message), 0);
                    current_state = STATE_CHAT_ROOM;
                }
            }
            else if (choice == 4)
            { //  Invite Friend to Room
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
            { // List Friends
                sprintf(message, "%d|", REQ_LIST_FRIENDS);
                send(sock, message, strlen(message), 0);
                sleep(1);
            }
            else if (choice == 6)
            { // Add Friend
                printf("Nhap username muon ket ban: ");
                fgets(target_name, sizeof(target_name), stdin);
                target_name[strcspn(target_name, "\n")] = 0;

                sprintf(message, "%d|%s", REQ_FRIEND_REQ, target_name);
                send(sock, message, strlen(message), 0);
                sleep(1);
            }
            else if (choice == 7)
            {
                // 1. Bước chuẩn bị: Nhấn Enter để xem
                printf("\n\033[1;36m[HUONG DAN]\033[0m: Nhan [Enter] de hien thi danh sach loi moi...");
                char wait_enter[10];
                fgets(wait_enter, sizeof(wait_enter), stdin);

                // 2. Gửi lệnh lấy danh sách
                sprintf(message, "%d|", REQ_FRIEND_ACCEPT);
                send(sock, message, strlen(message), 0);

                // Đợi 0.5s để luồng Recv kịp in danh sách ra màn hình trước
                usleep(500000);

                // 3. THỰC HIỆN THAO TÁC NGAY TẠI ĐÂY
                printf("\n\033[1;32m----------------- LUA CHON -----------------\033[0m\n");
                printf("=> Nhap PHIM TAT (A1, A2...) de dong y.\n");
                printf("=> Nhap BAT KY ky tu nao khac de thoat ra Menu.\n");
                printf("--------------------------------------------\n");
                printf(">>> Nhap lua chon: ");
                fflush(stdout);

                // Đọc lệnh nhập của người dùng ngay tại Sub-menu này
                char sub_input[64];
                fgets(sub_input, sizeof(sub_input), stdin);
                sub_input[strcspn(sub_input, "\n")] = 0;

                // Kiểm tra xem có phải phím tắt A1, A2... không
                if ((sub_input[0] == 'A' || sub_input[0] == 'a') && strlen(sub_input) > 1)
                {
                    int idx = atoi(&sub_input[1]) - 1;
                    if (idx >= 0 && idx < friend_invite_count)
                    {
                        // Gửi lệnh chấp nhận lên Server ngay lập tức
                        sprintf(message, "%d|%s", REQ_FRIEND_ACCEPT, pending_friends[idx]);
                        send(sock, message, strlen(message), 0);

                        // Đợi một chút để nhận thông báo thành công từ Server trước khi hiện lại Menu
                        sleep(1);
                    }
                    else
                    {
                        printf("\n[LOI]: So thu tu khong hop le!\n");
                        sleep(1);
                    }
                }
                else
                {
                    printf("\nDang quay lai Menu chinh...\n");
                }

                // Kết thúc case 7, chương trình sẽ tự động lặp lại và hiện Main Menu
            }
            else if (choice == 8)
            { // History
                printf("Nhap username/nhom muon xem lich su: ");
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
                printf("--- ROOM: %s (Go 'LEAVE' de roi phong /'EXIT' de ra menu) ---\n", target_name);
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
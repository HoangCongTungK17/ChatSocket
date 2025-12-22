#include "user_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

// Đường dẫn tương đối từ thư mục chạy file thực thi (server_app)
// Đảm bảo bạn đã tạo thư mục: server/data/user_data
#define DATA_DIR "server/data/user_data"
#define ID_FILE "server/data/user_data/next_id.txt"

// Hàm lấy ID tiếp theo từ file
int get_next_id()
{
    int id = 1; // Mặc định bắt đầu từ 1
    FILE *f = fopen(ID_FILE, "r");
    if (f)
    {
        fscanf(f, "%d", &id);
        fclose(f);
    }
    return id;
}

// Hàm lưu ID tiếp theo
void save_next_id(int id)
{
    FILE *f = fopen(ID_FILE, "w");
    if (f)
    {
        fprintf(f, "%d", id);
        fclose(f);
    }
}

// Logic kiểm tra đăng nhập
int check_login(const char *username, const char *password)
{
    char info_path[512];
    // Đường dẫn file: server/data/user_data/username/info.txt
    sprintf(info_path, "%s/%s/info.txt", DATA_DIR, username);

    // 1. Kiểm tra file tồn tại
    if (access(info_path, F_OK) == -1)
    {
        return -1; // User không tồn tại
    }

    // 2. Đọc file để lấy pass
    FILE *f = fopen(info_path, "r");
    if (!f)
        return -1;

    int file_id = -1;
    char file_pass[64] = "";

    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        // Xử lý dòng chứa ID:
        if (strstr(line, "ID:"))
        {
            sscanf(line, "ID:%d", &file_id);
        }
        // Xử lý dòng chứa PASSWORD:
        else if (strstr(line, "PASSWORD:"))
        {
            sscanf(line, "PASSWORD:%s", file_pass);
            // Xóa ký tự xuống dòng nếu có
            file_pass[strcspn(file_pass, "\n")] = 0;
        }
    }
    fclose(f);

    // 3. So sánh pass
    if (strlen(file_pass) > 0 && strcmp(file_pass, password) == 0)
    {
        return file_id;
    }
    return -1; // Sai pass hoặc lỗi file
}

// Logic đăng ký
int register_user(const char *username, const char *password)
{
    char user_dir[512];
    sprintf(user_dir, "%s/%s", DATA_DIR, username);

    // 1. Kiểm tra user đã tồn tại chưa
    struct stat st = {0};
    if (stat(user_dir, &st) != -1)
    {
        return 0; // Đã tồn tại
    }

    // 2. Tạo thư mục user
    // Lưu ý: mkdir trên Linux cần quyền 0700 hoặc 0777
    if (mkdir(user_dir, 0700) == -1)
    {
        perror("Cannot create user directory");
        return 0;
    }

    // 3. Lấy ID mới
    int new_id = get_next_id();

    // 4. Tạo file info.txt
    char info_path[512];
    sprintf(info_path, "%s/info.txt", user_dir);

    FILE *f = fopen(info_path, "w");
    if (f)
    {
        fprintf(f, "ID:%d\n", new_id);
        fprintf(f, "PASSWORD:%s\n", password);
        fclose(f);

        // Cập nhật ID cho người sau
        save_next_id(new_id + 1);
        return 1; // Thành công
    }
    return 0; // Thất bại
}

// Gửi lời mời kết bạn
// Return: 1 (Success), 0 (Fail/User not found), 2 (Already friend/Requested)
int send_friend_request(const char *sender, const char *receiver)
{
    char receiver_path[512];
    sprintf(receiver_path, "%s/%s/info.txt", DATA_DIR, receiver);

    // 1. Check user tồn tại
    if (access(receiver_path, F_OK) == -1)
        return 0; // Không tồn tại

    // 2. Check xem đã là bạn chưa
    char friend_file[512];
    sprintf(friend_file, "%s/%s/friend.txt", DATA_DIR, sender);
    FILE *f = fopen(friend_file, "r");
    if (f)
    {
        char line[MAX_USERNAME];
        while (fgets(line, sizeof(line), f))
        {
            line[strcspn(line, "\n")] = 0;
            if (strcmp(line, receiver) == 0)
            {
                fclose(f);
                return 2;
            } // Đã là bạn
        }
        fclose(f);
    }

    // 3. Ghi vào file listreq.txt của người nhận
    char req_file[512];
    sprintf(req_file, "%s/%s/listreq.txt", DATA_DIR, receiver);

    // Check xem đã request chưa để tránh duplicate
    f = fopen(req_file, "r");
    if (f)
    {
        char line[MAX_USERNAME];
        while (fgets(line, sizeof(line), f))
        {
            line[strcspn(line, "\n")] = 0;
            if (strcmp(line, sender) == 0)
            {
                fclose(f);
                return 2;
            }
        }
        fclose(f);
    }

    f = fopen(req_file, "a");
    if (f)
    {
        fprintf(f, "%s\n", sender);
        fclose(f);
        return 1;
    }
    return 0;
}

// Chấp nhận kết bạn
// Logic: Xóa khỏi listreq.txt -> Thêm vào friend.txt của CẢ 2 NGƯỜI
int accept_friend_request(const char *me, const char *sender)
{
    char req_file[512], friend_file_me[512], friend_file_sender[512];
    sprintf(req_file, "%s/%s/listreq.txt", DATA_DIR, me);
    sprintf(friend_file_me, "%s/%s/friend.txt", DATA_DIR, me);
    sprintf(friend_file_sender, "%s/%s/friend.txt", DATA_DIR, sender);

    // 1. Đọc list request và loại bỏ sender
    FILE *f = fopen(req_file, "r");
    if (!f)
        return 0; // Không có file request

    char buffer[4096] = ""; // Buffer tạm lưu nội dung file mới
    char line[MAX_USERNAME];
    int found = 0;

    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\n")] = 0;
        if (strcmp(line, sender) != 0)
        {
            strcat(buffer, line);
            strcat(buffer, "\n");
        }
        else
        {
            found = 1;
        }
    }
    fclose(f);

    if (!found)
        return 0; // Không thấy lời mời

    // Ghi lại file listreq (đã xóa sender)
    f = fopen(req_file, "w");
    if (f)
    {
        fputs(buffer, f);
        fclose(f);
    }

    // 2. Thêm vào friend.txt của Me
    f = fopen(friend_file_me, "a");
    if (f)
    {
        fprintf(f, "%s\n", sender);
        fclose(f);
    }

    // 3. Thêm vào friend.txt của Sender
    f = fopen(friend_file_sender, "a");
    if (f)
    {
        fprintf(f, "%s\n", me);
        fclose(f);
    }

    return 1;
}

// Lấy danh sách lời mời kết bạn
void get_friend_requests(const char *username, char *buffer)
{
    char path[512];
    sprintf(path, "%s/%s/listreq.txt", DATA_DIR, username);
    FILE *f = fopen(path, "r");
    strcpy(buffer, "");

    if (f)
    {
        char line[MAX_USERNAME];
        while (fgets(line, sizeof(line), f))
        {
            line[strcspn(line, "\n")] = 0;
            if (strlen(line) > 0)
            {
                strcat(buffer, line);
                strcat(buffer, ", ");
            }
        }
        fclose(f);
    }
    else
    {
        strcpy(buffer, "No requests.");
    }
}
// [File: server/libs/user_manager.c hoặc cuối server/main.c]
#include <dirent.h> // Can thiet de quet thu muc

// Hoang Tung Sua - Cap nhat theo cau truc thu muc cua ban
void get_user_joined_rooms_server(const char *username, char *output)
{
    char chat_dir[] = "server/data/chat_data";
    struct dirent *entry;
    DIR *dp = opendir(chat_dir);

    output[0] = '\0';
    if (dp == NULL)
        return;

    while ((entry = readdir(dp)))
    {
        // Chỉ tìm các file bắt đầu bằng "ROOM_" và kết thúc bằng ".txt"
        if (strncmp(entry->d_name, "ROOM_", 5) == 0 && strstr(entry->d_name, ".txt"))
        {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", chat_dir, entry->d_name);

            FILE *f = fopen(path, "r");
            if (f)
            {
                char line[256];
                // Giả sử dòng đầu tiên của file ROOM_ là danh sách thành viên
                // Hoặc bạn quét toàn bộ file để xem username có từng nhắn tin/tham gia không
                if (fgets(line, sizeof(line), f))
                {
                    // Logic này tùy thuộc vào cách bạn ghi file.
                    // Ở đây tôi ví dụ quét xem username có xuất hiện trong file hay không
                    int found = 0;
                    rewind(f);
                    while (fgets(line, sizeof(line), f))
                    {
                        if (strstr(line, username))
                        {
                            found = 1;
                            break;
                        }
                    }

                    if (found)
                    {
                        char room_name[128];
                        // Cắt chữ "ROOM_" (5 ký tự) và đuôi ".txt" (4 ký tự)
                        int name_len = strlen(entry->d_name) - 5 - 4;
                        strncpy(room_name, entry->d_name + 5, name_len);
                        room_name[name_len] = '\0';

                        if (strlen(output) > 0)
                            strcat(output, ", ");
                        strcat(output, room_name);
                    }
                }
                fclose(f);
            }
        }
    }
    closedir(dp);
}
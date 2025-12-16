#ifndef USER_MANAGER_H
#define USER_MANAGER_H

#include "../../common/protocol.h"

int check_login(const char *username, const char *password);
int register_user(const char *username, const char *password);

int send_friend_request(const char *sender, const char *receiver);
int accept_friend_request(const char *me, const char *sender);
void get_friend_requests(const char *username, char *buffer);

#endif
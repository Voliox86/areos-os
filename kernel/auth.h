#ifndef AUTH_H
#define AUTH_H

#define AUTH_MAX_USER 32
#define AUTH_MAX_PASS 64
#define AUTH_PATH     "/etc/passwd"

int  auth_setup(void);
int  auth_verify(const char* username, const char* password);
void auth_add_user(const char* username, const char* password);
void auth_list_users(void);

#endif

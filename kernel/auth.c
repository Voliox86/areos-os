#include "kernel.h"
#include "auth.h"
#include "ext2.h"

static uint32_t hash_with_salt(const char* str, const char* salt) {
    uint32_t h = 5381;
    if (salt)
        while (*salt) h = ((h << 5) + h) + (uint8_t)*salt++;
    if (str)
        while (*str) h = ((h << 5) + h) + (uint8_t)*str++;
    return h;
}

static void format_entry(char* buf, uint32_t bufsz,
                         const char* user, const char* salt,
                         uint32_t hash) {
    snprintf(buf, bufsz, "%s:%s:%08x\n", user, salt, hash);
}

static int parse_entry(const char* buf,
                       char* user, uint32_t user_sz,
                       char* salt, uint32_t salt_sz,
                       uint32_t* hash) {
    const char* p = buf;
    int i = 0;
    while (*p && *p != ':' && i < (int)user_sz - 1) user[i++] = *p++;
    if (*p != ':') return -1;
    user[i] = '\0'; p++;
    i = 0;
    while (*p && *p != ':' && i < (int)salt_sz - 1) salt[i++] = *p++;
    if (*p != ':') return -1;
    salt[i] = '\0'; p++;
    *hash = 0;
    while (*p >= '0' && *p <= '9') { *hash = (*hash << 4) | (*p - '0'); p++; }
    while (*p >= 'a' && *p <= 'f') { *hash = (*hash << 4) | (*p - 'a' + 10); p++; }
    while (*p >= 'A' && *p <= 'F') { *hash = (*hash << 4) | (*p - 'A' + 10); p++; }
    return 0;
}

static int passwd_exists(void) {
    if (ext2_fs.block_size == 0) return 0;
    return ext2_resolve(AUTH_PATH) != 0;
}

static int read_passwd(char* buf, uint32_t sz) {
    if (ext2_fs.block_size == 0) return -1;
    return ext2_read_file(AUTH_PATH, buf, sz);
}

static int write_passwd(const char* buf, uint32_t len) {
    if (ext2_fs.block_size == 0) return -1;
    ext2_create_file(AUTH_PATH);
    return ext2_write_file(AUTH_PATH, buf, len);
}

int auth_setup(void) {
    if (ext2_fs.block_size == 0) return -1;
    if (!passwd_exists()) {
        const char* default_user = "nyx";
        const char* default_pass = "nyx";
        const char* default_salt = "nx";
        uint32_t h = hash_with_salt(default_pass, default_salt);
        char entry[128];
        format_entry(entry, sizeof(entry), default_user, default_salt, h);

        ext2_create_file(AUTH_PATH);
        if (ext2_write_file(AUTH_PATH, entry, strlen(entry)) > 0) {
            printf("[AUTH] Created default user 'nyx'\n");
            return 0;
        }
    } else {
        printf("[AUTH] User file found at %s\n", AUTH_PATH);
        return 0;
    }
    return -1;
}

void auth_add_user(const char* username, const char* password) {
    if (ext2_fs.block_size == 0) return;

    char existing[2048];
    int exlen = read_passwd(existing, sizeof(existing) - 1);
    if (exlen < 0) exlen = 0;
    existing[exlen] = '\0';

    char salt[16];
    for (int i = 0; i < 7; i++)
        salt[i] = "0123456789abcdef"[hash_with_salt(username, "salt") & 0xF];
    salt[7] = '\0';

    uint32_t h = hash_with_salt(password, salt);
    char new_entry[128];
    format_entry(new_entry, sizeof(new_entry), username, salt, h);

    char combined[2048];
    snprintf(combined, sizeof(combined), "%s%s", existing, new_entry);
    write_passwd(combined, strlen(combined));
}

int auth_verify(const char* username, const char* password) {
    if (ext2_fs.block_size == 0) {
        return (strcmp(username, "nyx") == 0 && strcmp(password, "nyx") == 0) ? 1 : 0;
    }

    char buf[2048];
    int len = read_passwd(buf, sizeof(buf) - 1);
    if (len < 0) {
        return (strcmp(username, "nyx") == 0 && strcmp(password, "nyx") == 0) ? 1 : 0;
    }
    buf[len] = '\0';

    char line[256];
    int li = 0;
    for (int i = 0; i <= len; i++) {
        if (buf[i] == '\n' || buf[i] == '\0') {
            line[li] = '\0';
            if (li > 0) {
                char file_user[AUTH_MAX_USER];
                char file_salt[16];
                uint32_t file_hash;
                if (parse_entry(line, file_user, sizeof(file_user),
                                file_salt, sizeof(file_salt), &file_hash) == 0) {
                    if (strcmp(file_user, username) == 0) {
                        uint32_t check = hash_with_salt(password, file_salt);
                        if (check == file_hash) return 1;
                    }
                }
            }
            li = 0;
        } else {
            if (li < 255) line[li++] = buf[i];
        }
    }
    return 0;
}

#include "kernel.h"
#include "auth.h"
#include "ext2.h"
#include "sha256.h"

#define XENC_KEY     "NyxOS_AUTH_v5.3"
#define XENC_KEY_LEN 15
#define MAX_FALLBACK_USERS 8
#define SALT_SECRET  "NyxOS_k3rn3l_s34lt_v5.3"
#define SALT_HEX_LEN 16

/* ------------------------------------------------------------------ */
/*  PBKDF2-HMAC-SHA256 wrapper                                        */
/* ------------------------------------------------------------------ */
static void hash_password(const char* password, const char* salt_hex,
                          uint32_t iterations, uint8_t out[SHA256_DIGEST_SIZE]) {
    uint8_t salt[8];
    for (int i = 0; i < 8; i++) {
        char c = salt_hex[i * 2];
        uint8_t nib = (c >= '0' && c <= '9') ? (c - '0') :
                      (c >= 'a' && c <= 'f') ? (c - 'a' + 10) : (c - 'A' + 10);
        salt[i] = nib << 4;
        c = salt_hex[i * 2 + 1];
        nib = (c >= '0' && c <= '9') ? (c - '0') :
              (c >= 'a' && c <= 'f') ? (c - 'a' + 10) : (c - 'A' + 10);
        salt[i] |= nib;
    }
    pbkdf2_hmac_sha256((const uint8_t*)password, strlen(password),
                       salt, 8, iterations, out);
}

static void gen_salt_hex(const char* username, char salt_hex[SALT_HEX_LEN + 1]) {
    uint8_t hash[SHA256_DIGEST_SIZE];
    hmac_sha256((const uint8_t*)SALT_SECRET, strlen(SALT_SECRET),
                (const uint8_t*)username, strlen(username), hash);
    static const char hexdig[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        salt_hex[i * 2]     = hexdig[(hash[i] >> 4) & 0xF];
        salt_hex[i * 2 + 1] = hexdig[hash[i] & 0xF];
    }
    salt_hex[SALT_HEX_LEN] = '\0';
}

/* ------------------------------------------------------------------ */
/*  XOR obfuscation for passwd file on disk                           */
/* ------------------------------------------------------------------ */
static void xor_buf(char* buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        buf[i] ^= XENC_KEY[i % XENC_KEY_LEN];
}

/* ------------------------------------------------------------------ */
/*  Password file entry format:  user:salt_hex:iterations:hex_hash\n  */
/* ------------------------------------------------------------------ */
static void format_entry(char* buf, uint32_t bufsz,
                         const char* user, const char* salt_hex,
                         uint32_t iterations, const uint8_t hash[SHA256_DIGEST_SIZE]) {
    char hex[SHA256_DIGEST_SIZE * 2 + 1];
    sha256_to_hex(hash, hex);
    snprintf(buf, bufsz, "%s:%s:%u:%s\n", user, salt_hex, iterations, hex);
}

static int parse_entry(const char* buf,
                       char* user, uint32_t user_sz,
                       char* salt_hex, uint32_t salt_sz,
                       uint32_t* iterations,
                       uint8_t hash[SHA256_DIGEST_SIZE]) {
    const char* p = buf;
    int i = 0;
    while (*p && *p != ':' && i < (int)user_sz - 1) user[i++] = *p++;
    if (*p != ':') return -1;
    user[i] = '\0'; p++;
    i = 0;
    while (*p && *p != ':' && i < (int)salt_sz - 1) salt_hex[i++] = *p++;
    if (*p != ':') return -1;
    salt_hex[i] = '\0'; p++;
    if (i != SALT_HEX_LEN) return -1;
    *iterations = 0;
    while (*p >= '0' && *p <= '9') { *iterations = *iterations * 10 + (*p - '0'); p++; }
    if (*p != ':') return -1;
    p++;
    for (int j = 0; j < SHA256_DIGEST_SIZE; j++) {
        uint8_t hi = 0, lo = 0;
        char c = *p++;
        if (c >= '0' && c <= '9') hi = c - '0';
        else if (c >= 'a' && c <= 'f') hi = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') hi = c - 'A' + 10;
        else return -1;
        c = *p++;
        if (c >= '0' && c <= '9') lo = c - '0';
        else if (c >= 'a' && c <= 'f') lo = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') lo = c - 'A' + 10;
        else return -1;
        hash[j] = (hi << 4) | lo;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Fallback: in-memory user table when no EXT2                       */
/* ------------------------------------------------------------------ */
typedef struct {
    char username[AUTH_MAX_USER];
    char salt_hex[SALT_HEX_LEN + 1];
    uint32_t iterations;
    uint8_t hash[SHA256_DIGEST_SIZE];
} fallback_user_t;

static fallback_user_t fb_users[MAX_FALLBACK_USERS];
static int fb_count = 0;

static void add_fallback(const char* user, const char* pass) {
    if (fb_count >= MAX_FALLBACK_USERS) return;
    fallback_user_t* u = &fb_users[fb_count++];
    strncpy(u->username, user, AUTH_MAX_USER - 1);
    u->username[AUTH_MAX_USER - 1] = '\0';
    gen_salt_hex(user, u->salt_hex);
    u->iterations = PBKDF2_ITERATIONS;
    hash_password(pass, u->salt_hex, u->iterations, u->hash);
}

static int fallback_verify(const char* user, const char* pass) {
    for (int i = 0; i < fb_count; i++) {
        if (strcmp(fb_users[i].username, user) == 0) {
            uint8_t check[SHA256_DIGEST_SIZE];
            hash_password(pass, fb_users[i].salt_hex, fb_users[i].iterations, check);
            int ok = 1;
            for (int j = 0; j < SHA256_DIGEST_SIZE; j++)
                if (check[j] != fb_users[i].hash[j]) ok = 0;
            if (ok) return 1;
        }
    }
    return 0;
}

static int fallback_add(const char* user, const char* pass) {
    if (fb_count >= MAX_FALLBACK_USERS) return -1;
    add_fallback(user, pass);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  EXT2 passwd file helpers (XOR-encrypted)                          */
/* ------------------------------------------------------------------ */
static int passwd_exists(void) {
    if (ext2_fs.block_size == 0) return 0;
    return ext2_resolve(AUTH_PATH) != 0;
}

static int read_passwd(char* buf, uint32_t sz) {
    if (ext2_fs.block_size == 0) return -1;
    int r = ext2_read_file(AUTH_PATH, buf, sz);
    if (r > 0) xor_buf(buf, r);
    return r;
}

static int write_passwd(const char* buf, uint32_t len) {
    if (ext2_fs.block_size == 0) return -1;
    char tmp[4096];
    uint32_t cplen = len < sizeof(tmp) ? len : sizeof(tmp) - 1;
    memcpy(tmp, buf, cplen);
    xor_buf(tmp, cplen);
    ext2_create_file(AUTH_PATH);
    return ext2_write_file(AUTH_PATH, tmp, cplen);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */
int auth_setup(void) {
    if (ext2_fs.block_size == 0 || !ext2_resolve(AUTH_PATH)) {
        add_fallback("nyx", "nyx");
        add_fallback("root", "root");
        add_fallback("admin", "admin");
        printf("[AUTH] EXT2 not available — %d fallback user(s) loaded (PBKDF2-HMAC-SHA256, %u iterations)\n",
               fb_count, PBKDF2_ITERATIONS);
        return 0;
    }

    if (!passwd_exists()) {
        char salt_hex[SALT_HEX_LEN + 1];
        gen_salt_hex("nyx", salt_hex);
        uint8_t hash[SHA256_DIGEST_SIZE];
        hash_password("nyx", salt_hex, PBKDF2_ITERATIONS, hash);
        char entry[128 + SHA256_DIGEST_SIZE * 2];
        format_entry(entry, sizeof(entry), "nyx", salt_hex, PBKDF2_ITERATIONS, hash);

        if (write_passwd(entry, strlen(entry)) > 0) {
            printf("[AUTH] Created default user 'nyx' (PBKDF2-HMAC-SHA256, %u iterations, XOR-encrypted)\n",
                   PBKDF2_ITERATIONS);
            return 0;
        }
    } else {
        printf("[AUTH] User file found at %s\n", AUTH_PATH);
        return 0;
    }
    return -1;
}

int auth_verify(const char* username, const char* password) {
    if (!username || !password) return 0;

    if (ext2_fs.block_size == 0 || !passwd_exists())
        return fallback_verify(username, password);

    char buf[2048];
    int len = read_passwd(buf, sizeof(buf) - 1);
    if (len < 0) return fallback_verify(username, password);
    buf[len] = '\0';

    char line[300];
    int li = 0;
    for (int i = 0; i <= len; i++) {
        if (buf[i] == '\n' || buf[i] == '\0') {
            line[li] = '\0';
            if (li > 0) {
                char file_user[AUTH_MAX_USER];
                char file_salt[SALT_HEX_LEN + 1];
                uint32_t file_iter;
                uint8_t file_hash[SHA256_DIGEST_SIZE];
                if (parse_entry(line, file_user, sizeof(file_user),
                                file_salt, sizeof(file_salt),
                                &file_iter, file_hash) == 0) {
                    if (strcmp(file_user, username) == 0) {
                        uint8_t check[SHA256_DIGEST_SIZE];
                        hash_password(password, file_salt, file_iter, check);
                        int ok = 1;
                        for (int j = 0; j < SHA256_DIGEST_SIZE; j++)
                            if (check[j] != file_hash[j]) ok = 0;
                        if (ok) return 1;
                    }
                }
            }
            li = 0;
        } else {
            if (li < 299) line[li++] = buf[i];
        }
    }
    return fallback_verify(username, password);
}

void auth_add_user(const char* username, const char* password) {
    if (!username || !password) return;

    if (ext2_fs.block_size == 0 || !passwd_exists()) {
        if (fallback_add(username, password) == 0)
            printf("[AUTH] Added fallback user '%s' (PBKDF2-HMAC-SHA256, %d users)\n",
                   username, fb_count);
        return;
    }

    char existing[3072];
    int exlen = read_passwd(existing, sizeof(existing) - 1);
    if (exlen < 0) exlen = 0;
    existing[exlen] = '\0';

    char salt_hex[SALT_HEX_LEN + 1];
    gen_salt_hex(username, salt_hex);
    uint8_t hash[SHA256_DIGEST_SIZE];
    hash_password(password, salt_hex, PBKDF2_ITERATIONS, hash);

    char new_entry[128 + SHA256_DIGEST_SIZE * 2];
    format_entry(new_entry, sizeof(new_entry), username, salt_hex, PBKDF2_ITERATIONS, hash);

    char combined[4096];
    snprintf(combined, sizeof(combined), "%s%s", existing, new_entry);
    if (write_passwd(combined, strlen(combined)) > 0)
        printf("[AUTH] Added user '%s' (PBKDF2-HMAC-SHA256, %u iterations)\n",
               username, PBKDF2_ITERATIONS);
}

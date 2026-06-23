#include "kernel.h"
#include "ext2.h"

#define MAX_INODES    128
#define MAX_NAME      64
#define MAX_CHILDREN  64
#define BLOCK_SIZE    512

// Mount table
static mount_entry_t mount_table[MAX_MOUNT_POINTS];
static int mount_count = 0;

typedef struct vfs_node {
    char name[MAX_NAME];
    uint32_t type;         // 0=file, 1=dir
    uint32_t size;
    uint8_t* data;
    uint32_t node_id;
    struct vfs_node* parent;
    struct vfs_node* children[MAX_CHILDREN];
    uint32_t child_count;
} vfs_node_t;

static vfs_node_t nodes[MAX_INODES];
static uint32_t node_count = 0;
static vfs_node_t* current_dir = NULL;

static vfs_node_t* alloc_node(void) {
    if (node_count >= MAX_INODES) return NULL;
    vfs_node_t* node = &nodes[node_count++];
    memset_asm(node, 0, sizeof(vfs_node_t));
    node->node_id = node_count - 1;
    return node;
}

static vfs_node_t* find_child(vfs_node_t* dir, const char* name) {
    for (uint32_t i = 0; i < dir->child_count; i++) {
        if (strcmp(dir->children[i]->name, name) == 0)
            return dir->children[i];
    }
    return NULL;
}

static char* path_token(char* path, char* token) {
    if (!path || !*path) return NULL;
    while (*path == '/') path++;
    if (!*path) return NULL;
    int i = 0;
    while (*path && *path != '/' && i < MAX_NAME-1) {
        token[i++] = *path++;
    }
    token[i] = '\0';
    return path;
}

static vfs_node_t* resolve_path(const char* path) {
    if (!path || !*path) return current_dir;
    char buf[256];
    strncpy(buf, path, 255);
    char* p = buf;

    vfs_node_t* dir;
    if (buf[0] == '/') {
        dir = &nodes[0];
        p++;
    } else {
        dir = current_dir;
    }

    char token[MAX_NAME];
    vfs_node_t* result = dir;
    while ((p = path_token(p, token)) != NULL && *token) {
        result = find_child(dir, token);
        if (!result || result->type != 1) return NULL;
        dir = result;
    }
    if (*token) {
        result = find_child(dir, token);
        if (!result) return NULL;
    }
    return result;
}

static vfs_node_t* resolve_parent(const char* path, char* child_name) {
    if (!path || !*path) return NULL;
    char buf[256];
    strncpy(buf, path, 255);
    char* p = buf;
    if (buf[0] == '/') p++;

    vfs_node_t* dir = (buf[0] == '/') ? &nodes[0] : current_dir;

    char token[MAX_NAME];
    char prev[MAX_NAME] = "";
    vfs_node_t* result = dir;
    while ((p = path_token(p, token)) != NULL && *token) {
        strncpy(prev, token, MAX_NAME-1);
        vfs_node_t* next = find_child(dir, token);
        if (!next) {
            if (child_name) strncpy(child_name, token, MAX_NAME-1);
            return dir;
        }
        if (next->type != 1) {
            if (child_name) strncpy(child_name, token, MAX_NAME-1);
            return dir;
        }
        dir = next;
    }
    if (child_name) strncpy(child_name, prev, MAX_NAME-1);
    return result;
}

void init_vfs(void) {
    node_count = 0;
    vfs_node_t* root = alloc_node();
    strcpy(root->name, "/");
    root->type = 1;
    root->parent = root;
    current_dir = root;

    // Create some default entries
    vfs_mkdir("/home", 0755);
    vfs_mkdir("/home/user", 0755);
    vfs_mkdir("/tmp", 0755);
    vfs_mkdir("/etc", 0755);
    vfs_mkdir("/var", 0755);
    vfs_mkdir("/opt", 0755);
    vfs_mkdir("/dev", 0755);
    vfs_mkdir("/bin", 0755);
    vfs_mkdir("/proc", 0755);
    vfs_mkdir("/sys", 0755);

    // Create welcome file
    int fd = vfs_open("/home/user/welcome.txt", 1, 0644);
    if (fd >= 0) {
        vfs_write(fd, "Welcome to NyxOS v1.0.0\n", 24);
        vfs_write(fd, "Type 'help' for commands.\n", 26);
        vfs_close(fd);
    }
}

int vfs_open(const char* path, int flags, mode_t mode) {
    (void)mode;
    vfs_node_t* ino = resolve_path(path);

    if (flags & 1) { // O_WRONLY / O_CREAT
        if (!ino) {
            char child_name[MAX_NAME];
            vfs_node_t* parent = resolve_parent(path, child_name);
            if (!parent || parent->type != 1) return -1;
            ino = alloc_node();
            if (!ino) return -1;
            strncpy(ino->name, child_name, MAX_NAME-1);
            ino->type = 0;
            ino->parent = parent;
            parent->children[parent->child_count++] = ino;
        }
        return (int)(uint32_t)ino;
    }

    if (!ino || ino->type != 0) return -1;
    return (int)(uint32_t)ino;
}

int vfs_read(int fd, void* buf, size_t count) {
    vfs_node_t* ino = (vfs_node_t*)(uint32_t)fd;
    if (!ino || ino->type != 0) return -1;
    if (count > ino->size) count = ino->size;
    if (ino->data) memcpy(buf, ino->data, count);
    return count;
}

int vfs_write(int fd, const void* buf, size_t count) {
    vfs_node_t* ino = (vfs_node_t*)(uint32_t)fd;
    if (!ino || ino->type != 0) return -1;
    if (!ino->data || ino->size < count) {
        uint8_t* new_data = (uint8_t*)kmalloc(count + BLOCK_SIZE);
        if (!new_data) return -1;
        if (ino->data) {
            memcpy(new_data, ino->data, ino->size);
            kfree(ino->data);
        }
        ino->data = new_data;
    }
    memcpy(ino->data, buf, count);
    ino->size = count;
    return count;
}

int vfs_close(int fd) {
    (void)fd;
    return 0;
}

int vfs_create_from_mem(const char* path, uint8_t* data, uint32_t size) {
    char child_name[MAX_NAME];
    vfs_node_t* parent = resolve_parent((char*)path, child_name);
    if (!parent || parent->type != 1) return -1;
    vfs_node_t* ino = alloc_node();
    if (!ino) return -1;
    strncpy(ino->name, child_name, MAX_NAME-1);
    ino->type = 0;
    ino->size = size;
    ino->data = data;
    ino->parent = parent;
    parent->children[parent->child_count++] = ino;
    return (int)(uint32_t)ino;
}

uint32_t vfs_fsize(int fd) {
    vfs_node_t* ino = (vfs_node_t*)(uint32_t)fd;
    if (!ino || ino->type != 0) return 0;
    return ino->size;
}

uint8_t* vfs_fdata(int fd) {
    vfs_node_t* ino = (vfs_node_t*)(uint32_t)fd;
    if (!ino || ino->type != 0) return NULL;
    return ino->data;
}

int vfs_mkdir(const char* path, mode_t mode) {
    (void)mode;
    char child_name[MAX_NAME];
    vfs_node_t* parent = resolve_parent(path, child_name);
    if (!parent || parent->type != 1) return -1;
    if (find_child(parent, child_name)) return -1;

    vfs_node_t* dir = alloc_node();
    if (!dir) return -1;
    strncpy(dir->name, child_name, MAX_NAME-1);
    dir->type = 1;
    dir->parent = parent;
    parent->children[parent->child_count++] = dir;
    return 0;
}

int vfs_unlink(const char* path) {
    char child_name[MAX_NAME];
    vfs_node_t* parent = resolve_parent(path, child_name);
    if (!parent || parent->type != 1) return -1;
    for (uint32_t i = 0; i < parent->child_count; i++) {
        if (strcmp(parent->children[i]->name, child_name) == 0) {
            if (parent->children[i]->data) kfree(parent->children[i]->data);
            memset_asm(parent->children[i], 0, sizeof(vfs_node_t));
            for (uint32_t j = i; j < parent->child_count - 1; j++)
                parent->children[j] = parent->children[j+1];
            parent->child_count--;
            return 0;
        }
    }
    return -1;
}

void hide_file(const char* path) {
    (void)path;
}

void vfs_rename(const char* old, const char* new) {
    char child_name[MAX_NAME];
    vfs_node_t* parent = resolve_parent(old, child_name);
    if (!parent) return;
    vfs_node_t* ino = find_child(parent, child_name);
    if (!ino) return;
    char new_name[MAX_NAME];
    vfs_node_t* new_parent = resolve_parent(new, new_name);
    if (!new_parent) { strncpy(ino->name, new, MAX_NAME-1); return; }
    strncpy(ino->name, new_name, MAX_NAME-1);
    if (new_parent != parent) {
        for (uint32_t i = 0; i < parent->child_count; i++) {
            if (parent->children[i] == ino) {
                for (uint32_t j = i; j < parent->child_count - 1; j++)
                    parent->children[j] = parent->children[j+1];
                parent->child_count--;
                break;
            }
        }
        new_parent->children[new_parent->child_count++] = ino;
        ino->parent = new_parent;
    }
}

dirent_t* vfs_readdir(int fd) {
    (void)fd;
    return NULL;
}

// ==================== Mount table ====================

int vfs_mount(const char* mount_point, int fs_type, void* fs_data) {
    if (mount_count >= MAX_MOUNT_POINTS) return -1;
    mount_entry_t* me = &mount_table[mount_count];
    strncpy(me->mount_point, mount_point, MAX_PATH - 1);
    me->type = fs_type;
    me->resolve = NULL;
    me->get_size = NULL;
    me->read_file = NULL;
    me->readdir = NULL;

    if (fs_type == FS_TYPE_EXT2) {
        me->resolve  = ext2_resolve;
        me->get_size = ext2_get_size;
        me->read_file = ext2_read_file;
        me->readdir   = ext2_readdir;
    }

    mount_count++;
    return 0;
}

mount_entry_t* vfs_find_mount(const char* path) {
    if (!path) return NULL;
    for (int i = 0; i < mount_count; i++) {
        int len = strlen(mount_table[i].mount_point);
        if (strncmp(path, mount_table[i].mount_point, len) == 0) {
            if (path[len] == '\0' || path[len] == '/')
                return &mount_table[i];
        }
    }
    return NULL;
}

// ==================== New helper functions ====================

const char* vfs_getcwd(void) {
    return current_dir ? current_dir->name : "/";
}

int vfs_chdir(const char* path) {
    mount_entry_t* me = vfs_find_mount(path);
    if (me) {
        // Find or create a stub directory for the mount point
        vfs_node_t* dir = resolve_path(path);
        if (!dir) {
            // Auto-create the mount point directory in ramdisk VFS
            char child_name[MAX_NAME];
            vfs_node_t* parent = resolve_parent(path, child_name);
            if (!parent || parent->type != 1) return -1;
            dir = alloc_node();
            if (!dir) return -1;
            strncpy(dir->name, child_name, MAX_NAME - 1);
            dir->type = 1;
            dir->parent = parent;
            parent->children[parent->child_count++] = dir;
        }
        current_dir = dir;
        return 0;
    }

    vfs_node_t* dir = resolve_path(path);
    if (!dir || dir->type != 1) return -1;
    current_dir = dir;
    return 0;
}

void vfs_list_dir(const char* path) {
    mount_entry_t* me = vfs_find_mount(path);
    if (me && me->readdir) {
        dirent_t entries[64];
        int n = me->readdir(path, entries, 64);
        if (n < 0) {
            printf("ls: %s: error reading directory\n", path ? path : "");
            return;
        }
        for (int i = 0; i < n; i++) {
            if (entries[i].type == 1) {
                set_terminal_color(vga_entry_color(VGA_LIGHT_BLUE, VGA_BLACK));
                printf("%s/\n", entries[i].name);
            } else {
                set_terminal_color(vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK));
                printf("%s\n", entries[i].name);
            }
        }
        set_terminal_color(vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK));
        return;
    }

    vfs_node_t* dir = path ? resolve_path(path) : current_dir;
    if (!dir || dir->type != 1) {
        printf("ls: %s: No such directory\n", path ? path : "");
        return;
    }
    for (uint32_t i = 0; i < dir->child_count; i++) {
        vfs_node_t* child = dir->children[i];
        if (child->type == 1) {
            set_terminal_color(vga_entry_color(VGA_LIGHT_BLUE, VGA_BLACK));
            printf("%s/\n", child->name);
        } else {
            set_terminal_color(vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK));
            printf("%s  (%d bytes)\n", child->name, child->size);
        }
    }
    set_terminal_color(vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK));
}

void vfs_cat_file(const char* path) {
    mount_entry_t* me = vfs_find_mount(path);
    if (me && me->read_file) {
        char buf[512];
        int n = me->read_file(path, buf, 511);
        if (n < 0) {
            printf("cat: %s: error reading file\n", path);
            return;
        }
        buf[n] = '\0';
        printf("%s", buf);
        return;
    }

    vfs_node_t* ino = resolve_path(path);
    if (!ino || ino->type != 0) {
        printf("cat: %s: No such file\n", path);
        return;
    }
    for (uint32_t i = 0; i < ino->size; i++) {
        putchar(ino->data[i]);
    }
}

int vfs_touch(const char* path) {
    vfs_node_t* ino = resolve_path(path);
    if (ino) return 0; // exists
    char child_name[MAX_NAME];
    vfs_node_t* parent = resolve_parent(path, child_name);
    if (!parent || parent->type != 1) return -1;
    ino = alloc_node();
    if (!ino) return -1;
    strncpy(ino->name, child_name, MAX_NAME-1);
    ino->type = 0;
    ino->parent = parent;
    parent->children[parent->child_count++] = ino;
    return 0;
}

int vfs_cp(const char* src, const char* dst) {
    vfs_node_t* src_ino = resolve_path(src);
    if (!src_ino || src_ino->type != 0) return -1;
    char child_name[MAX_NAME];
    vfs_node_t* dst_parent = resolve_parent(dst, child_name);
    if (!dst_parent || dst_parent->type != 1) return -1;
    if (find_child(dst_parent, child_name)) return -1;
    vfs_node_t* dst_ino = alloc_node();
    if (!dst_ino) return -1;
    strncpy(dst_ino->name, child_name, MAX_NAME-1);
    dst_ino->type = 0;
    dst_ino->parent = dst_parent;
    dst_ino->size = src_ino->size;
    if (src_ino->size > 0 && src_ino->data) {
        dst_ino->data = (uint8_t*)kmalloc(src_ino->size);
        if (dst_ino->data) memcpy(dst_ino->data, src_ino->data, src_ino->size);
    }
    dst_parent->children[dst_parent->child_count++] = dst_ino;
    return 0;
}

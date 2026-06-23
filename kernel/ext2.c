#include "kernel.h"
#include "ata.h"
#include "ext2.h"

ext2_fs_t ext2_fs;

static uint8_t* block_buf = NULL;
static uint32_t block_buf_size = 0;

static void read_sectors(uint32_t lba, uint8_t count, void* buf) {
    ata_read_sectors(ext2_fs.drive, ext2_fs.part_start_lba + lba, count, buf);
}

static uint32_t block_to_lba(uint32_t block) {
    uint32_t block_size = ext2_fs.block_size;
    uint32_t sectors_per_block = block_size / 512;
    return block * sectors_per_block;
}

static uint8_t* get_block_buf(void) {
    if (block_buf && block_buf_size >= ext2_fs.block_size)
        return block_buf;
    if (block_buf) kfree(block_buf);
    block_buf = (uint8_t*)kmalloc(ext2_fs.block_size);
    if (block_buf) block_buf_size = ext2_fs.block_size;
    return block_buf;
}

static int read_block_group_bgd(uint32_t group, ext2_bgd_t* bgd) {
    uint32_t bgd_per_block = ext2_fs.block_size / sizeof(ext2_bgd_t);
    uint32_t bgd_block_offset = group / bgd_per_block;
    uint32_t bgd_in_block = group % bgd_per_block;

    uint32_t block = ext2_fs.bgd_block + bgd_block_offset;
    uint8_t* buf = get_block_buf();
    if (!buf) return -1;

    ext2_read_block(block, buf);
    __builtin_memcpy(bgd, buf + bgd_in_block * sizeof(ext2_bgd_t), sizeof(ext2_bgd_t));
    return 0;
}

void init_ext2(void) {
    block_buf = NULL;
    block_buf_size = 0;
}

int ext2_mount(uint8_t drive, uint32_t part_lba) {
    ext2_fs.drive = drive;
    ext2_fs.part_start_lba = part_lba;

    uint8_t sb_buf[1024];
    read_sectors(2, 2, sb_buf);
    __builtin_memcpy(&ext2_fs.sb, sb_buf, sizeof(ext2_superblock_t));

    if (ext2_fs.sb.magic != EXT2_SUPER_MAGIC)
        return -1;

    ext2_fs.block_size = 1024 << ext2_fs.sb.log_block_size;
    ext2_fs.inodes_per_group = ext2_fs.sb.inodes_per_group;
    ext2_fs.blocks_per_group = ext2_fs.sb.blocks_per_group;
    ext2_fs.inode_size = ext2_fs.sb.inode_size ? ext2_fs.sb.inode_size : 128;

    if (ext2_fs.block_size == 1024)
        ext2_fs.bgd_block = 2;
    else
        ext2_fs.bgd_block = 1;

    uint32_t block_groups = (ext2_fs.sb.total_blocks + ext2_fs.blocks_per_group - 1) / ext2_fs.blocks_per_group;
    uint32_t bgd_size = block_groups * sizeof(ext2_bgd_t);
    ext2_fs.bgd_blocks = (bgd_size + ext2_fs.block_size - 1) / ext2_fs.block_size;

    return 0;
}

int ext2_read_inode(uint32_t ino, ext2_inode_t* inode) {
    if (ino == 0) return -1;
    uint32_t group = (ino - 1) / ext2_fs.inodes_per_group;
    uint32_t index = (ino - 1) % ext2_fs.inodes_per_group;

    ext2_bgd_t bgd;
    if (read_block_group_bgd(group, &bgd) < 0) return -1;

    uint32_t inode_table_block = bgd.inode_table;
    uint32_t bytes_per_inode = ext2_fs.inode_size;
    uint32_t inodes_per_block = ext2_fs.block_size / bytes_per_inode;
    uint32_t block = inode_table_block + index / inodes_per_block;
    uint32_t offset_in_block = (index % inodes_per_block) * bytes_per_inode;

    uint8_t* buf = get_block_buf();
    if (!buf) return -1;

    ext2_read_block(block, buf);
    __builtin_memcpy(inode, buf + offset_in_block, sizeof(ext2_inode_t));
    return 0;
}

int ext2_read_block(uint32_t block, void* buf) {
    uint32_t lba = block_to_lba(block);
    uint32_t sectors = ext2_fs.block_size / 512;
    for (uint32_t i = 0; i < sectors; i++)
        read_sectors(lba + i, 1, (uint8_t*)buf + i * 512);
    return 0;
}

int ext2_read_inode_block(ext2_inode_t* inode, uint32_t iblock, void* buf) {
    uint32_t ptrs_per_block = ext2_fs.block_size / 4;

    if (iblock < 12) {
        if (inode->block[iblock] == 0) return -1;
        return ext2_read_block(inode->block[iblock], buf);
    }

    iblock -= 12;
    if (iblock < ptrs_per_block) {
        if (inode->block[12] == 0) return -1;
        uint32_t* indirect = (uint32_t*)get_block_buf();
        ext2_read_block(inode->block[12], indirect);
        if (indirect[iblock] == 0) return -1;
        return ext2_read_block(indirect[iblock], buf);
    }

    iblock -= ptrs_per_block;
    if (iblock < ptrs_per_block * ptrs_per_block) {
        if (inode->block[13] == 0) return -1;
        uint32_t* dindirect = (uint32_t*)get_block_buf();
        ext2_read_block(inode->block[13], dindirect);
        uint32_t block_idx = dindirect[iblock / ptrs_per_block];
        if (block_idx == 0) return -1;
        uint32_t* indirect = (uint32_t*)get_block_buf();
        ext2_read_block(block_idx, indirect);
        uint32_t target = indirect[iblock % ptrs_per_block];
        if (target == 0) return -1;
        return ext2_read_block(target, buf);
    }

    return -1;
}

static uint32_t resolve_path(const char* path) {
    if (!path || path[0] != '/') return 0;
    if (path[1] == '\0') return EXT2_ROOT_INO;

    uint32_t cur_ino = EXT2_ROOT_INO;
    char component[256];
    const char* p = path + 1;

    while (*p) {
        int ci = 0;
        while (*p && *p != '/') component[ci++] = *p++;
        component[ci] = '\0';
        if (*p == '/') p++;

        if (ci == 0) continue;

        ext2_inode_t dir_inode;
        if (ext2_read_inode(cur_ino, &dir_inode) < 0) return 0;

        int found = 0;
        uint32_t bytes_left = dir_inode.size;
        uint32_t iblock = 0;

        while (bytes_left > 0 && !found) {
            uint8_t* block_buf_local = get_block_buf();
            if (ext2_read_inode_block(&dir_inode, iblock, block_buf_local) < 0) break;
            uint32_t off = 0;
            while (off < ext2_fs.block_size && off < bytes_left) {
                ext2_dirent_t* de = (ext2_dirent_t*)(block_buf_local + off);
                if (de->inode == 0) { off += de->rec_len; continue; }
                char dname[256];
                uint8_t name_len = de->name_len & 0xFF;
                __builtin_memcpy(dname, de->name, name_len);
                dname[name_len] = '\0';
                if (strcmp(dname, component) == 0) {
                    cur_ino = de->inode;
                    found = 1;
                    break;
                }
                off += de->rec_len;
            }
            bytes_left -= ext2_fs.block_size;
            iblock++;
        }

        if (!found) return 0;
    }

    return cur_ino;
}

// ========== VFS driver functions ==========

uint32_t ext2_resolve(const char* path) {
    return resolve_path(path);
}

uint32_t ext2_get_size(const char* path) {
    uint32_t ino = resolve_path(path);
    if (!ino) return 0;
    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) < 0) return 0;
    return inode.size;
}

int ext2_read_file(const char* path, void* buf, uint32_t maxlen) {
    uint32_t ino = resolve_path(path);
    if (!ino) return -1;

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) < 0) return -1;

    if (!(inode.mode & EXT2_S_IFREG)) return -1;

    uint32_t remaining = inode.size;
    if (remaining > maxlen) remaining = maxlen;
    uint32_t total = 0;
    uint32_t iblock = 0;

    while (remaining > 0) {
        uint8_t* block_buf_local = get_block_buf();
        if (ext2_read_inode_block(&inode, iblock, block_buf_local) < 0) break;
        uint32_t chunk = (remaining < ext2_fs.block_size) ? remaining : ext2_fs.block_size;
        __builtin_memcpy((uint8_t*)buf + total, block_buf_local, chunk);
        total += chunk;
        remaining -= chunk;
        iblock++;
    }

    return total;
}

int ext2_readdir(const char* path, dirent_t* entries, uint32_t max_entries) {
    uint32_t ino = resolve_path(path);
    if (!ino) return -1;

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) < 0) return -1;

    if (!(inode.mode & EXT2_S_IFDIR)) return -1;

    int count = 0;
    uint32_t bytes_left = inode.size;
    uint32_t iblock = 0;

    while (bytes_left > 0 && count < (int)max_entries) {
        uint8_t* block_buf_local = get_block_buf();
        if (ext2_read_inode_block(&inode, iblock, block_buf_local) < 0) break;
        uint32_t off = 0;
        while (off < ext2_fs.block_size && off < bytes_left && count < (int)max_entries) {
            ext2_dirent_t* de = (ext2_dirent_t*)(block_buf_local + off);
            if (de->inode == 0) { off += de->rec_len ? de->rec_len : 8; continue; }
            uint8_t name_len = de->name_len & 0xFF;
            if (name_len > 0) {
                __builtin_memcpy(entries[count].name, de->name, name_len);
                entries[count].name[name_len] = '\0';
                entries[count].ino = de->inode;
                entries[count].type = (de->file_type == EXT2_FT_DIR) ? 1 : 0;
                count++;
            }
            off += de->rec_len;
        }
        bytes_left -= ext2_fs.block_size;
        iblock++;
    }

    return count;
}

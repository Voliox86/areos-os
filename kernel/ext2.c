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
                if (de->inode == 0 || de->rec_len < 1) { off += (de->rec_len < 1) ? 1 : de->rec_len; continue; }
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

// ========== EXT2 Write Operations ==========

static void write_sectors(uint32_t lba, uint8_t count, const void* buf) {
    ata_write_sectors(ext2_fs.drive, ext2_fs.part_start_lba + lba, count, buf);
}

int ext2_write_block(uint32_t block, const void* buf) {
    uint32_t lba = block_to_lba(block);
    uint32_t sectors = ext2_fs.block_size / 512;
    for (uint32_t i = 0; i < sectors; i++)
        write_sectors(lba + i, 1, (const uint8_t*)buf + i * 512);
    return 0;
}

int ext2_write_inode(uint32_t ino, const ext2_inode_t* inode) {
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
    __builtin_memcpy(buf + offset_in_block, inode, sizeof(ext2_inode_t));
    ext2_write_block(block, buf);
    return 0;
}

static void write_bgd(uint32_t group, const ext2_bgd_t* bgd) {
    uint32_t bgd_per_block = ext2_fs.block_size / sizeof(ext2_bgd_t);
    uint32_t bgd_block_offset = group / bgd_per_block;
    uint32_t bgd_in_block = group % bgd_per_block;
    uint32_t block = ext2_fs.bgd_block + bgd_block_offset;
    uint8_t* buf = get_block_buf();
    if (!buf) return;
    ext2_read_block(block, buf);
    __builtin_memcpy(buf + bgd_in_block * sizeof(ext2_bgd_t), bgd, sizeof(ext2_bgd_t));
    ext2_write_block(block, buf);
}

int ext2_sync_bgd(uint32_t group) {
    ext2_bgd_t bgd;
    if (read_block_group_bgd(group, &bgd) < 0) return -1;
    write_bgd(group, &bgd);
    return 0;
}

int ext2_sync_superblock(void) {
    uint8_t sb_buf[1024];
    __builtin_memset(sb_buf, 0, 1024);
    __builtin_memcpy(sb_buf, &ext2_fs.sb, sizeof(ext2_superblock_t));
    write_sectors(2, 2, sb_buf);
    return 0;
}

// Find a free bit in a bitmap (block or inode)
static int find_free_bit(const uint8_t* bitmap, uint32_t size_bits) {
    for (uint32_t i = 0; i < size_bits; i++) {
        if (!(bitmap[i / 8] & (1 << (i % 8))))
            return i;
    }
    return -1;
}

uint32_t ext2_alloc_block(void) {
    ext2_bgd_t bgd;
    if (read_block_group_bgd(0, &bgd) < 0) return 0;

    uint8_t* buf = get_block_buf();
    if (!buf) return 0;

    ext2_read_block(bgd.block_bitmap, buf);

    uint32_t blocks_in_group = ext2_fs.blocks_per_group;
    int bit = find_free_bit(buf, blocks_in_group);
    if (bit < 0) return 0;

    uint32_t block_num = (uint32_t)bit; // group 0 blocks

    // Mark bit as used
    buf[bit / 8] |= (1 << (bit % 8));
    ext2_write_block(bgd.block_bitmap, buf);

    // Update BGD
    if (bgd.free_blocks_count > 0) bgd.free_blocks_count--;
    write_bgd(0, &bgd);

    // Update superblock
    if (ext2_fs.sb.free_blocks > 0) ext2_fs.sb.free_blocks--;

    // Zero out the block before returning
    __builtin_memset(buf, 0, ext2_fs.block_size);
    ext2_write_block(block_num, buf);

    return block_num;
}

uint32_t ext2_alloc_inode(void) {
    ext2_bgd_t bgd;
    if (read_block_group_bgd(0, &bgd) < 0) return 0;

    uint8_t* buf = get_block_buf();
    if (!buf) return 0;

    ext2_read_block(bgd.inode_bitmap, buf);

    uint32_t inodes_in_group = ext2_fs.inodes_per_group;
    int bit = find_free_bit(buf, inodes_in_group);
    if (bit < 0) return 0;

    uint32_t ino = bit + 1;

    // Mark bit as used
    buf[bit / 8] |= (1 << (bit % 8));
    ext2_write_block(bgd.inode_bitmap, buf);

    // Update BGD
    if (bgd.free_inodes_count > 0) bgd.free_inodes_count--;
    write_bgd(0, &bgd);

    // Update superblock
    if (ext2_fs.sb.free_inodes > 0) ext2_fs.sb.free_inodes--;

    return ino;
}

int ext2_write_file(const char* path, const void* buf, uint32_t len) {
    uint32_t ino = ext2_resolve(path);
    if (!ino) {
        if (ext2_create_file(path) < 0) return -1;
        ino = ext2_resolve(path);
        if (!ino) return -1;
    }

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) < 0) return -1;

    if (!(inode.mode & EXT2_S_IFREG)) return -1;

    // Calculate how many blocks needed
    uint32_t blocks_needed = (len + ext2_fs.block_size - 1) / ext2_fs.block_size;

    // Write data block by block
    const uint8_t* data = (const uint8_t*)buf;
    uint32_t remaining = len;
    uint32_t iblock = 0;

    while (remaining > 0) {
        uint32_t block_num = 0;
        uint32_t chunk = (remaining < ext2_fs.block_size) ? remaining : ext2_fs.block_size;
        uint8_t block_buf[4096];
        __builtin_memset(block_buf, 0, sizeof(block_buf));
        __builtin_memcpy(block_buf, data, chunk);

        if (iblock < 12) {
            if (inode.block[iblock] == 0) {
                inode.block[iblock] = ext2_alloc_block();
                if (inode.block[iblock] == 0) return -1;
            }
            block_num = inode.block[iblock];
        } else {
            uint32_t ptrs_per_block = ext2_fs.block_size / 4;
            uint32_t sind_iblock = iblock - 12;
            if (sind_iblock < ptrs_per_block) {
                if (inode.block[12] == 0) {
                    inode.block[12] = ext2_alloc_block();
                    if (inode.block[12] == 0) return -1;
                    __builtin_memset(block_buf, 0, ext2_fs.block_size);
                    ext2_write_block(inode.block[12], block_buf);
                }
                uint8_t* ibuf = get_block_buf();
                ext2_read_block(inode.block[12], ibuf);
                uint32_t* indirect = (uint32_t*)ibuf;
                if (indirect[sind_iblock] == 0) {
                    indirect[sind_iblock] = ext2_alloc_block();
                    if (indirect[sind_iblock] == 0) return -1;
                    ext2_write_block(inode.block[12], ibuf);
                }
                block_num = indirect[sind_iblock];
            } else {
                return -1;
            }
        }

        ext2_write_block(block_num, block_buf);
        data += chunk;
        remaining -= chunk;
        iblock++;
    }

    // Clear old blocks beyond new size (file shrunk)
    // For simplicity, we leave them allocated but the inode's block[] for them is left as-is
    // A proper implementation would free them

    // Update inode size and block count
    inode.size = len;
    inode.blocks_512 = blocks_needed * (ext2_fs.block_size / 512);

    // Write inode back
    ext2_write_inode(ino, &inode);

    return len;
}

int ext2_create_file(const char* path) {
    if (!path || path[0] != '/') return -1;
    // Check if already exists
    if (ext2_resolve(path)) return -1;

    // Split into parent dir and filename
    char parent_path[256];
    char filename[256];
    int last_slash = -1;
    for (int i = 0; path[i]; i++)
        if (path[i] == '/') last_slash = i;

    if (last_slash <= 0) {
        // File at root
        parent_path[0] = '/'; parent_path[1] = '\0';
    } else {
        memcpy(parent_path, path, last_slash);
        parent_path[last_slash] = '\0';
    }
    strcpy(filename, path + last_slash + 1);

    uint32_t parent_ino = ext2_resolve(parent_path);
    if (!parent_ino) return -1;

    ext2_inode_t parent_inode;
    if (ext2_read_inode(parent_ino, &parent_inode) < 0) return -1;
    if (!(parent_inode.mode & EXT2_S_IFDIR)) return -1;

    // Allocate inode
    uint32_t new_ino = ext2_alloc_inode();
    if (!new_ino) return -1;

    // Initialize new inode
    ext2_inode_t new_inode;
    memset_asm(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.mode = EXT2_S_IFREG | 0x1A4; // 0644
    new_inode.uid = 0;
    new_inode.gid = 0;
    new_inode.size = 0;
    new_inode.links_count = 1;
    new_inode.blocks_512 = 0;

    if (ext2_write_inode(new_ino, &new_inode) < 0) return -1;

    // Add directory entry in parent
    uint32_t name_len = strlen(filename);
    uint32_t entry_size = sizeof(ext2_dirent_t) - 255 + name_len;
    entry_size = (entry_size + 3) & ~3; // align to 4

    // Find a block with space or allocate a new one
    uint32_t iblock = 0;
    uint8_t* buf = get_block_buf();
    if (!buf) return -1;
    int found_space = 0;

    while (iblock < 12 && iblock * ext2_fs.block_size < parent_inode.size) {
        if (parent_inode.block[iblock] == 0) break;
        ext2_read_block(parent_inode.block[iblock], buf);

        uint32_t off = 0;
        while (off < ext2_fs.block_size) {
            ext2_dirent_t* de = (ext2_dirent_t*)(buf + off);
            uint32_t de_name_len = de->name_len & 0xFF;
            if (de->inode == 0) {
                // Unused entry - check if it has space
                if (de->rec_len >= entry_size + 4) {
                    // Reuse this spot
                    de->inode = new_ino;
                    de->name_len = name_len;
                    de->file_type = EXT2_FT_REG_FILE;
                    memset_asm(de->name, 0, de->rec_len - 4);
                    memcpy(de->name, filename, name_len);
                    ext2_write_block(parent_inode.block[iblock], buf);
                    found_space = 1;
                    break;
                }
            } else {
                // Check if this is the last entry in the block
                uint32_t min_size = sizeof(ext2_dirent_t) - 255 + de_name_len;
                min_size = (min_size + 3) & ~3;
                if (de->rec_len > min_size + entry_size) {
                    // Shrink last entry, add new one after
                    uint32_t remaining = de->rec_len - min_size;
                    de->rec_len = min_size;
                    ext2_dirent_t* new_de = (ext2_dirent_t*)(buf + off + min_size);
                    new_de->inode = new_ino;
                    new_de->rec_len = remaining;
                    new_de->name_len = name_len;
                    new_de->file_type = EXT2_FT_REG_FILE;
                    memset_asm(new_de->name, 0, remaining - 4);
                    memcpy(new_de->name, filename, name_len);
                    ext2_write_block(parent_inode.block[iblock], buf);
                    found_space = 1;
                    break;
                }
            }
            off += de->rec_len;
        }
        if (found_space) break;
        iblock++;
    }

    if (!found_space) {
        // Allocate a new block for the directory
        uint32_t new_block = ext2_alloc_block();
        if (!new_block) return -1;
        memset_asm(buf, 0, ext2_fs.block_size);
        ext2_dirent_t* de = (ext2_dirent_t*)buf;
        de->inode = new_ino;
        de->rec_len = ext2_fs.block_size;
        de->name_len = name_len;
        de->file_type = EXT2_FT_REG_FILE;
        memcpy(de->name, filename, name_len);
        ext2_write_block(new_block, buf);

        // Link block to parent inode
        if (iblock < 12) {
            parent_inode.block[iblock] = new_block;
        } else {
            return -1; // too many blocks for direct
        }
        parent_inode.size += ext2_fs.block_size;
    }

    parent_inode.links_count++; // or not, dir links don't increment for files
    ext2_write_inode(parent_ino, &parent_inode);

    return 0;
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

// ========== Block and inode freeing ==========

static void ext2_free_block(uint32_t block_num) {
    ext2_bgd_t bgd;
    if (read_block_group_bgd(0, &bgd) < 0) return;
    uint8_t* buf = get_block_buf();
    if (!buf) return;
    ext2_read_block(bgd.block_bitmap, buf);
    if (buf[block_num / 8] & (1 << (block_num % 8))) {
        buf[block_num / 8] &= ~(1 << (block_num % 8));
        ext2_write_block(bgd.block_bitmap, buf);
        bgd.free_blocks_count++;
        write_bgd(0, &bgd);
        ext2_fs.sb.free_blocks++;
    }
}

static void free_inode_blocks(ext2_inode_t* inode) {
    uint32_t ptrs_per_block = ext2_fs.block_size / 4;
    // Free direct blocks
    for (int i = 0; i < 12; i++) {
        if (inode->block[i]) { ext2_free_block(inode->block[i]); inode->block[i] = 0; }
    }
    // Free singly-indirect block
    if (inode->block[12]) {
        uint8_t* buf = get_block_buf();
        ext2_read_block(inode->block[12], buf);
        uint32_t* indirect = (uint32_t*)buf;
        for (uint32_t i = 0; i < ptrs_per_block; i++)
            if (indirect[i]) ext2_free_block(indirect[i]);
        ext2_free_block(inode->block[12]);
        inode->block[12] = 0;
    }
    // Free doubly-indirect block
    if (inode->block[13]) {
        uint8_t* buf = get_block_buf();
        ext2_read_block(inode->block[13], buf);
        uint32_t* dindirect = (uint32_t*)buf;
        for (uint32_t i = 0; i < ptrs_per_block; i++) {
            if (dindirect[i]) {
                uint8_t* ibuf = get_block_buf();
                ext2_read_block(dindirect[i], ibuf);
                uint32_t* indirect = (uint32_t*)ibuf;
                for (uint32_t j = 0; j < ptrs_per_block; j++)
                    if (indirect[j]) ext2_free_block(indirect[j]);
                ext2_free_block(dindirect[i]);
            }
        }
        ext2_free_block(inode->block[13]);
        inode->block[13] = 0;
    }
}

static void ext2_free_inode(uint32_t ino) {
    if (ino < EXT2_ROOT_INO) return;
    uint32_t group = (ino - 1) / ext2_fs.inodes_per_group;
    uint32_t index = (ino - 1) % ext2_fs.inodes_per_group;
    ext2_bgd_t bgd;
    if (read_block_group_bgd(group, &bgd) < 0) return;
    uint8_t* buf = get_block_buf();
    if (!buf) return;
    ext2_read_block(bgd.inode_bitmap, buf);
    if (buf[index / 8] & (1 << (index % 8))) {
        buf[index / 8] &= ~(1 << (index % 8));
        ext2_write_block(bgd.inode_bitmap, buf);
        bgd.free_inodes_count++;
        write_bgd(0, &bgd);
        ext2_fs.sb.free_inodes++;
    }
}

// ========== Directory helpers ==========

static int add_dirent_to_parent(uint32_t parent_ino, const char* name,
                                 uint32_t new_ino, uint8_t file_type)
{
    ext2_inode_t parent_inode;
    if (ext2_read_inode(parent_ino, &parent_inode) < 0) return -1;
    if (!(parent_inode.mode & EXT2_S_IFDIR)) return -1;

    uint32_t name_len = strlen(name);
    uint32_t entry_size = sizeof(ext2_dirent_t) - 255 + name_len;
    entry_size = (entry_size + 3) & ~3;

    uint32_t iblock = 0;
    uint8_t* buf = get_block_buf();
    if (!buf) return -1;
    int found_space = 0;

    while (iblock < 12 && iblock * ext2_fs.block_size < parent_inode.size) {
        if (parent_inode.block[iblock] == 0) break;
        ext2_read_block(parent_inode.block[iblock], buf);
        uint32_t off = 0;
        while (off < ext2_fs.block_size) {
            ext2_dirent_t* de = (ext2_dirent_t*)(buf + off);
            uint32_t de_name_len = de->name_len & 0xFF;
            if (de->inode == 0) {
                if (de->rec_len >= entry_size + 4) {
                    de->inode = new_ino;
                    de->name_len = name_len;
                    de->file_type = file_type;
                    __builtin_memset(de->name, 0, de->rec_len - 4);
                    __builtin_memcpy(de->name, name, name_len);
                    ext2_write_block(parent_inode.block[iblock], buf);
                    found_space = 1;
                    break;
                }
            } else {
                uint32_t min_size = sizeof(ext2_dirent_t) - 255 + de_name_len;
                min_size = (min_size + 3) & ~3;
                if (de->rec_len > min_size + entry_size) {
                    uint32_t remaining = de->rec_len - min_size;
                    de->rec_len = min_size;
                    ext2_dirent_t* new_de = (ext2_dirent_t*)(buf + off + min_size);
                    new_de->inode = new_ino;
                    new_de->rec_len = remaining;
                    new_de->name_len = name_len;
                    new_de->file_type = file_type;
                    __builtin_memset(new_de->name, 0, remaining - 4);
                    __builtin_memcpy(new_de->name, name, name_len);
                    ext2_write_block(parent_inode.block[iblock], buf);
                    found_space = 1;
                    break;
                }
            }
            off += de->rec_len;
        }
        if (found_space) break;
        iblock++;
    }

    if (!found_space) {
        uint32_t new_block = ext2_alloc_block();
        if (!new_block) return -1;
        __builtin_memset(buf, 0, ext2_fs.block_size);
        ext2_dirent_t* de = (ext2_dirent_t*)buf;
        de->inode = new_ino;
        de->rec_len = ext2_fs.block_size;
        de->name_len = name_len;
        de->file_type = file_type;
        __builtin_memcpy(de->name, name, name_len);
        ext2_write_block(new_block, buf);
        if (iblock < 12) {
            parent_inode.block[iblock] = new_block;
        } else {
            return -1;
        }
        parent_inode.size += ext2_fs.block_size;
    }

    ext2_write_inode(parent_ino, &parent_inode);
    return 0;
}

int ext2_mkdir(const char* path) {
    if (!path || path[0] != '/') return -1;
    if (ext2_resolve(path)) return -1;

    char parent_path[256];
    char dirname[256];
    int last_slash = -1;
    for (int i = 0; path[i]; i++)
        if (path[i] == '/') last_slash = i;

    if (last_slash <= 0) {
        parent_path[0] = '/'; parent_path[1] = '\0';
    } else {
        __builtin_memcpy(parent_path, path, last_slash);
        parent_path[last_slash] = '\0';
    }
    strcpy(dirname, path + last_slash + 1);

    uint32_t parent_ino = ext2_resolve(parent_path);
    if (!parent_ino) return -1;

    uint32_t new_ino = ext2_alloc_inode();
    if (!new_ino) return -1;

    ext2_inode_t new_inode;
    __builtin_memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.mode = EXT2_S_IFDIR | 0x1FF; // 0777
    new_inode.uid = 0;
    new_inode.gid = 0;
    new_inode.size = ext2_fs.block_size;
    new_inode.links_count = 2; // . and ..
    new_inode.blocks_512 = ext2_fs.block_size / 512;

    // Allocate first block for . and .. entries
    uint32_t first_block = ext2_alloc_block();
    if (!first_block) { ext2_free_inode(new_ino); return -1; }
    new_inode.block[0] = first_block;

    uint8_t* buf = get_block_buf();
    if (!buf) { ext2_free_block(first_block); ext2_free_inode(new_ino); return -1; }
    __builtin_memset(buf, 0, ext2_fs.block_size);

    // Entry for "."
    ext2_dirent_t* de = (ext2_dirent_t*)buf;
    de->inode = new_ino;
    de->rec_len = 12;
    de->name_len = 1;
    de->file_type = EXT2_FT_DIR;
    de->name[0] = '.';

    // Entry for ".."
    ext2_dirent_t* de2 = (ext2_dirent_t*)(buf + 12);
    de2->inode = parent_ino;
    de2->rec_len = ext2_fs.block_size - 12;
    de2->name_len = 2;
    de2->file_type = EXT2_FT_DIR;
    de2->name[0] = '.'; de2->name[1] = '.';

    ext2_write_block(first_block, buf);

    if (ext2_write_inode(new_ino, &new_inode) < 0) {
        ext2_free_block(first_block);
        ext2_free_inode(new_ino);
        return -1;
    }

    // Add entry in parent
    if (add_dirent_to_parent(parent_ino, dirname, new_ino, EXT2_FT_DIR) < 0) {
        free_inode_blocks(&new_inode);
        ext2_free_inode(new_ino);
        return -1;
    }

    // Increment parent's link count for ".."
    ext2_inode_t pinode;
    if (ext2_read_inode(parent_ino, &pinode) == 0) {
        pinode.links_count++;
        ext2_write_inode(parent_ino, &pinode);
    }

    return 0;
}

int ext2_unlink(const char* path) {
    if (!path || path[0] != '/') return -1;
    uint32_t ino = ext2_resolve(path);
    if (!ino || ino < EXT2_ROOT_INO) return -1;

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) < 0) return -1;

    // Check if directory is empty (only . and ..)
    if (inode.mode & EXT2_S_IFDIR) {
        ext2_inode_t dir_inode = inode;
        uint32_t count = 0;
        uint32_t bytes_left = dir_inode.size;
        uint32_t iblock = 0;
        while (bytes_left > 0) {
            uint8_t* dbuf = get_block_buf();
            if (ext2_read_inode_block(&dir_inode, iblock, dbuf) < 0) break;
            uint32_t off = 0;
            while (off < ext2_fs.block_size && off < bytes_left) {
                ext2_dirent_t* de = (ext2_dirent_t*)(dbuf + off);
                if (de->inode && (de->name_len & 0xFF) > 2) count++;
                if (de->inode && (de->name_len & 0xFF) > 2) break;
                off += de->rec_len;
            }
            bytes_left -= ext2_fs.block_size;
            iblock++;
        }
        if (count > 0) return -1; // directory not empty
    }

    char parent_path[256];
    char child_name[256];
    int last_slash = -1;
    for (int i = 0; path[i]; i++)
        if (path[i] == '/') last_slash = i;

    if (last_slash <= 0) {
        parent_path[0] = '/'; parent_path[1] = '\0';
    } else {
        __builtin_memcpy(parent_path, path, last_slash);
        parent_path[last_slash] = '\0';
    }
    strcpy(child_name, path + last_slash + 1);

    uint32_t parent_ino = ext2_resolve(parent_path);
    if (!parent_ino) return -1;

    // Remove directory entry from parent
    ext2_inode_t parent_inode;
    if (ext2_read_inode(parent_ino, &parent_inode) < 0) return -1;

    uint32_t child_name_len = strlen(child_name);
    int removed = 0;
    uint32_t piblock = 0;
    uint32_t pbytes_left = parent_inode.size;

    while (pbytes_left > 0 && !removed) {
        uint8_t* pbuf = get_block_buf();
        if (ext2_read_inode_block(&parent_inode, piblock, pbuf) < 0) break;
        uint32_t off = 0;
        while (off < ext2_fs.block_size && off < pbytes_left) {
            ext2_dirent_t* de = (ext2_dirent_t*)(pbuf + off);
            uint32_t de_name_len = de->name_len & 0xFF;
            if (de->inode && de_name_len == child_name_len) {
                char dname[256];
                __builtin_memcpy(dname, de->name, de_name_len);
                dname[de_name_len] = '\0';
                if (strcmp(dname, child_name) == 0) {
                    de->inode = 0;
                    ext2_write_block(parent_inode.block[piblock], pbuf);
                    removed = 1;
                    break;
                }
            }
            off += de->rec_len;
        }
        pbytes_left -= ext2_fs.block_size;
        piblock++;
    }

    if (!removed) return -1;

    // If directory, decrement parent link count
    if (inode.mode & EXT2_S_IFDIR) {
        parent_inode.links_count--;
    }

    // Decrement inode link count
    if (inode.links_count > 0) {
        inode.links_count--;
        if (inode.links_count == 0) {
            // Free all blocks and inode
            free_inode_blocks(&inode);
            ext2_write_inode(ino, &inode);
            ext2_free_inode(ino);
        } else {
            ext2_write_inode(ino, &inode);
        }
    }

    ext2_write_inode(parent_ino, &parent_inode);
    return 0;
}

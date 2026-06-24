#include "kernel.h"

extern void* syscall_table[SYS_TABLE_SIZE];

void init_syscalls(void) {
    memset_asm(syscall_table, 0, sizeof(syscall_table));
}

static process_t* get_cur_proc(void) {
    extern process_t* get_current_process(void);
    return get_current_process();
}

uint32_t syscall_handler_c(uint32_t no, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a4; (void)a5;
    switch (no) {
        case SYS_EXIT: {  // exit(status)
            printf("[USER] exit(%u)\n", a1);
            process_t* cur = get_cur_proc();
            if (cur) cur->state = 0;
            __asm__ volatile("sti; hlt");
            return 0;
        }
        case SYS_WRITE: {  // write(fd, buf, len)
            int fd = (int)a1;
            const char* buf = (const char*)a2;
            int len = (int)a3;
            if (fd == 1 || fd == 2) {
                for (int i = 0; i < len; i++) putchar(buf[i]);
            }
            return len;
        }
        case SYS_PRINT: {  // print string
            printf("%s", (const char*)a1);
            return 0;
        }
        case SYS_OPEN: {  // open(path, flags, mode)
            const char* path = (const char*)a1;
            int flags = (int)a2;
            int mode = (int)a3;
            return vfs_open(path, flags, mode);
        }
        case SYS_READ: {  // read(fd, buf, count)
            int fd = (int)a1;
            void* buf = (void*)a2;
            int count = (int)a3;
            return vfs_read(fd, buf, count);
        }
        case SYS_CLOSE: {  // close(fd)
            return vfs_close((int)a1);
        }
        case SYS_GETPID: {  // getpid()
            process_t* cur = get_cur_proc();
            return cur ? cur->pid : 0;
        }
        case SYS_SBRK: {  // sbrk(increment)
            process_t* cur = get_cur_proc();
            if (!cur) return -1;
            int inc = (int)a1;
            uint32_t old_brk = cur->program_break;
            if (inc == 0) return old_brk;
            if (inc < 0) return -1; // We don't support shrinking
            uint32_t new_brk = old_brk + inc;
            // Allocate pages as needed (up to stack at 0xD0000000)
            uint32_t start_page = (old_brk + 0xFFF) & ~0xFFF;
            uint32_t end_page = (new_brk + 0xFFF) & ~0xFFF;
            if (end_page > 0xD0000000) return -1;
            uint32_t* pd = (uint32_t*)cur->page_directory;
            for (uint32_t page = start_page; page < end_page; page += 4096) {
                void* phys = alloc_page();
                if (!phys) return -1;
                map_page_dir(pd, phys, (void*)page, 0x7); // rwx user
            }
            cur->program_break = new_brk;
            return old_brk;
        }
        case SYS_FSIZE: {  // fsize(fd)
            return vfs_fsize((int)a1);
        }
        default:
            printf("[SYSCALL] Unknown syscall %u\n", no);
            return -1;
    }
}

void* get_syscall_table(void) {
    return syscall_table;
}

void register_syscall(uint32_t num, void* handler) {
    if (num < SYS_TABLE_SIZE) syscall_table[num] = handler;
}

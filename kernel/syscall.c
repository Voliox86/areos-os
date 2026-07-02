#include "kernel.h"
#include "elf.h"

extern void* syscall_table[SYS_TABLE_SIZE];

void init_syscalls(void) {
    memset_asm(syscall_table, 0, sizeof(syscall_table));
}

static process_t* get_cur_proc(void) {
    extern process_t* get_current_process(void);
    return get_current_process();
}

// Set up syscall MSRs for the syscall/sysret mechanism
void setup_syscall_msrs(void) {
    // Enable SYSCALL/SYSRET (EFER.SCE). Without this the `syscall` instruction
    // raises #UD in ring 3 — the reason user processes crashed on entry.
    write_msr(MSR_EFER, read_msr(MSR_EFER) | EFER_SCE);

    // STAR:  [63:48] = sysret CS for ring 3, [47:32] = syscall CS (ring 0)
    //        [31:0]  = not used (legacy SYSCALL EIP)
    uint64_t star = ((uint64_t)USER_CS << 48) | ((uint64_t)KERNEL_CS << 32);
    write_msr(MSR_STAR, star);

    // LSTAR: RIP of syscall entry point
    write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

    // SF_MASK: clear IF (bit 9) and DF (bit 10) during syscall
    write_msr(MSR_SF_MASK, (1 << 9) | (1 << 10));
}

// Provide kernel stack to user processes via the syscall_entry global variable
extern uint64_t user_rsp;
extern uint64_t kernel_rsp;

void set_kernel_rsp(uint64_t rsp) {
    kernel_rsp = rsp;
}

/* ------------------------------------------------------------------ */
/*  Ring-3 syscall boundary guards                                    */
/* ------------------------------------------------------------------ */
/* Canonical user space is the lower half: [USER_SPACE_MIN, USER_SPACE_END).
 * Anything at or above USER_SPACE_END is non-canonical or the higher-half
 * kernel. Rejecting those stops a ring-3 process from handing the kernel a
 * kernel address as a syscall buffer (which would be an arbitrary kernel
 * read/write or info-leak primitive). */
#define USER_SPACE_MIN 0x1000ULL
#define USER_SPACE_END 0x0000800000000000ULL

static int user_ptr_ok(uint64_t ptr, uint64_t len) {
    if (ptr < USER_SPACE_MIN) return 0;
    if (len > USER_SPACE_END) return 0;
    if (ptr + len < ptr) return 0;              /* wrap-around */
    if (ptr + len > USER_SPACE_END) return 0;
    return 1;
}

static int user_str_ok(uint64_t ptr) {
    /* String length is unknown here; validating the base pointer keeps a
     * higher-half kernel address from ever being dereferenced as a string. */
    return ptr >= USER_SPACE_MIN && ptr < USER_SPACE_END;
}

/* Per-process-agnostic fd table: ring 3 gets small integer fds and never sees
 * (or can forge) a raw kernel VFS handle. 0-2 are the standard streams. */
#define UFD_BASE 3
#define UFD_MAX  32
static int  ufd_handle[UFD_MAX];    /* internal VFS handle for each slot */
static char ufd_inuse[UFD_MAX];

static int ufd_alloc(int internal) {
    for (int i = 0; i < UFD_MAX; i++) {
        if (!ufd_inuse[i]) { ufd_inuse[i] = 1; ufd_handle[i] = internal; return UFD_BASE + i; }
    }
    return -1;
}
static int ufd_lookup(int ufd, int* internal) {
    int i = ufd - UFD_BASE;
    if (i < 0 || i >= UFD_MAX || !ufd_inuse[i]) return -1;
    *internal = ufd_handle[i];
    return 0;
}
static void ufd_release(int ufd) {
    int i = ufd - UFD_BASE;
    if (i >= 0 && i < UFD_MAX) ufd_inuse[i] = 0;
}

uint64_t syscall_handler(uint64_t no, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    switch (no) {
        case SYS_EXIT: {
            printf("[USER] exit(%lu)\n", a1);
            process_t* cur = get_cur_proc();
            if (cur) cur->state = 0;
            for (;;) __asm__ volatile("hlt");
            return 0;
        }
        case SYS_WRITE: {
            int fd = (int)a1;
            const char* buf = (const char*)a2;
            int len = (int)a3;
            if (len < 0 || !user_ptr_ok(a2, (uint64_t)len)) return -1;
            if (fd == 1 || fd == 2) {
                for (int i = 0; i < len; i++) putchar(buf[i]);
            }
            return len;
        }
        case SYS_PRINT: {
            if (!user_str_ok(a1)) return -1;
            printf("%s", (const char*)a1);
            return 0;
        }
        case SYS_OPEN: {
            if (!user_str_ok(a1)) return -1;
            const char* path = (const char*)a1;
            int flags = (int)a2;
            int mode = (int)a3;
            int internal = vfs_open(path, flags, mode);
            if (internal < 0) return -1;
            int ufd = ufd_alloc(internal);
            if (ufd < 0) { vfs_close(internal); return -1; }  /* table full */
            return ufd;
        }
        case SYS_READ: {
            int internal;
            if (ufd_lookup((int)a1, &internal) != 0) return -1;
            int count = (int)a3;
            if (count < 0 || !user_ptr_ok(a2, (uint64_t)count)) return -1;
            return vfs_read(internal, (void*)a2, count);
        }
        case SYS_CLOSE: {
            int internal;
            if (ufd_lookup((int)a1, &internal) != 0) return -1;
            ufd_release((int)a1);
            return vfs_close(internal);
        }
        case SYS_GETPID: {
            process_t* cur = get_cur_proc();
            return cur ? cur->pid : 0;
        }
        case SYS_SBRK: {
            process_t* cur = get_cur_proc();
            if (!cur) return -1;
            int64_t inc = (int64_t)a1;
            uint64_t old_brk = cur->program_break;
            if (inc == 0) return old_brk;
            if (inc < 0) return -1;
            uint64_t new_brk = old_brk + inc;
            uint64_t start_page = (old_brk + 0xFFF) & ~0xFFFULL;
            uint64_t end_page = (new_brk + 0xFFF) & ~0xFFFULL;
            if (end_page > 0x100000000ULL) return -1;
            uint64_t* pml4 = (uint64_t*)cur->page_directory;
            for (uint64_t page = start_page; page < end_page; page += 4096) {
                void* phys = alloc_page();
                if (!phys) return -1;
                map_page_dir(pml4, phys, (void*)page, 0x7 | PAGE_NX);
            }
            cur->program_break = new_brk;
            return old_brk;
        }
        case SYS_FSIZE: {
            int internal;
            if (ufd_lookup((int)a1, &internal) != 0) return -1;
            return vfs_fsize(internal);
        }
        case SYS_EXEC: {
            if (!user_str_ok(a1)) return -1;
            const char* path = (const char*)a1;
            int fd = vfs_open(path, 0, 0);      /* kernel-side handle, never exposed */
            if (fd < 0) return -1;
            uint32_t size = vfs_fsize(fd);
            uint8_t* data = vfs_fdata(fd);
            if (!data || size == 0) { vfs_close(fd); return -1; }
            uint8_t* copy = (uint8_t*)kmalloc(size);
            if (!copy) { vfs_close(fd); return -1; }
            memcpy_asm(copy, data, size);
            vfs_close(fd);
            if (!elf_validate(copy, size)) { kfree(copy); return -2; }
            process_t* new_proc = NULL;
            int ret = elf_load(copy, size, &new_proc);
            kfree(copy);
            if (ret != 0 || !new_proc) return -3;
            printf("[EXEC] Loaded %s as PID %lu\n", path, new_proc->pid);
            return (uint64_t)new_proc->pid;
        }
        default:
            printf("[SYSCALL] Unknown syscall %lu\n", no);
            return -1;
    }
}

void* get_syscall_table(void) {
    return syscall_table;
}

void register_syscall(uint32_t num, void* handler) {
    if (num < SYS_TABLE_SIZE) syscall_table[num] = handler;
}

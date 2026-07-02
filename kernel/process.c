#include "kernel.h"

extern process_t* process_table[MAX_PROCESSES];
extern int process_count;
static uint64_t next_pid = 1;

int current_idx = 0;

void init_process(void) {
    memset_asm(process_table, 0, sizeof(process_table));
    process_t* init = (process_t*)kmalloc(sizeof(process_t));
    if (init) {
        memset_asm(init, 0, sizeof(process_t));
        init->pid = next_pid++;
        init->ppid = 0;
        init->state = 1;
        strncpy(init->comm, "init", 31);
        // page_directory left NULL -- scheduler uses kernel_pml4_phys (physical addr)
        process_table[process_count++] = init;
    }
}

// Set up a kernel stack for a new process.
// Stack layout (from low to high addr) matches switch_context restore:
//   r15, r14, r13, r12, r11, r10, r9, r8,
//   rbp, rdi, rsi, rdx, rcx, rbx, rax, rip_old
// Then after SAVE_REGS (old iret frame):
//   r15, r14, r13, r12, r11, r10, r9, r8,
//   rbp, rdi, rsi, rdx, rcx, rbx, rax,
//   int_no(32), error(0), rip, cs, rflags, rsp, ss
static int init_task_stack(process_t* proc, void* entry_point) {
    void* stack_mem = kmalloc(4096);
    if (!stack_mem) return -1;
    uint64_t* sp = (uint64_t*)((uintptr_t)stack_mem + 4096);

    // iretq frame for kernel process (ring 0)
    *--sp = 0x08;              // ss = kernel data
    *--sp = (uint64_t)(uintptr_t)stack_mem + 4096; // rsp
    *--sp = 0x202;             // rflags (IF set)
    *--sp = 0x08;              // cs = kernel code
    *--sp = (uint64_t)(uintptr_t)entry_point; // rip

    *--sp = 0;                 // error code
    *--sp = 32;                // int number (irq0)

    // SAVE_REGS: 15 zeros
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;

    proc->stack = (void*)((uintptr_t)sp);
    proc->kernel_stack = (void*)((uintptr_t)stack_mem + 4096);
    return 0;
}

static int init_user_task_stack(process_t* proc, void* entry_point, void* user_stack_top) {
    void* stack_mem = kmalloc(4096);
    if (!stack_mem) return -1;
    uint64_t* sp = (uint64_t*)((uintptr_t)stack_mem + 4096);

    // iretq frame for user process (ring 3)
    // When iretq detects CS.DPL != current CPL, it pops SS:RSP too (5 values)
    *--sp = USER_DS;            // ss = user data (ring 3)
    *--sp = (uint64_t)(uintptr_t)user_stack_top; // rsp = user stack
    *--sp = 0x202;              // rflags (IF set)
    *--sp = USER_CS;            // cs = user code (ring 3, L-bit=1)
    *--sp = (uint64_t)(uintptr_t)entry_point; // rip

    *--sp = 0;                  // error code
    *--sp = 32;                 // int number (irq0)

    // SAVE_REGS: 15 zeros
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;

    proc->stack = (void*)(uintptr_t)sp;
    proc->kernel_stack = (void*)((uintptr_t)stack_mem + 4096);
    return 0;
}

process_t* create_process(const char* name, void* entry, uint64_t flags) {
    (void)flags;
    if (process_count >= MAX_PROCESSES) return NULL;
    process_t* p = (process_t*)kmalloc(sizeof(process_t));
    if (!p) return NULL;
    memset_asm(p, 0, sizeof(process_t));
    p->pid = next_pid++;
    p->ppid = 0;
    p->state = 1;
    strncpy(p->comm, name, 31);
    p->comm[31] = '\0';
    if (entry) {
        if (init_task_stack(p, entry) < 0) {
            kfree(p);
            return NULL;
        }
    }
    process_table[process_count++] = p;
    return p;
}

process_t* create_user_process(const char* name, void* entry, void* user_stack, uint64_t* page_dir) {
    if (process_count >= MAX_PROCESSES) return NULL;
    process_t* p = (process_t*)kmalloc(sizeof(process_t));
    if (!p) return NULL;
    memset_asm(p, 0, sizeof(process_t));
    p->pid = next_pid++;
    p->state = 1;
    p->page_directory = page_dir;
    strncpy(p->comm, name, 31);
    p->comm[31] = '\0';

    if (!user_stack) {
        void* stack_page = alloc_page();
        if (!stack_page) { kfree(p); return NULL; }
        uint64_t stack_virt = 0x00007FFFFFFFE000ULL;
        map_page_dir(page_dir, stack_page, (void*)stack_virt, 0x7 | PAGE_NX);
        user_stack = (void*)(stack_virt + 4096);
    }

    if (init_user_task_stack(p, entry, user_stack) < 0) {
        kfree(p);
        return NULL;
    }
    process_table[process_count++] = p;
    return p;
}

extern void switch_to_user_trampoline(void);

void switch_to_user_process(process_t* proc) {
    if (!proc || !proc->page_directory) return;
    tss_set_stack((uint64_t)(uintptr_t)proc->kernel_stack + KERNEL_BASE);
    uint64_t tramp = (uint64_t)switch_to_user_trampoline + KERNEL_BASE;
    uint64_t rsp_val = (uint64_t)(uintptr_t)proc->stack;
    uint64_t cr3_val = (uint64_t)proc->page_directory;
    __asm__ volatile(
        "cli \n"
        "mov %0, %%rdi \n"
        "mov %1, %%rsi \n"
        "mov %2, %%rax \n"
        "call *%%rax \n"
        :: "r"(rsp_val), "r"(cr3_val), "r"(tramp)
        : "rdi", "rsi", "rax"
    );
    for (;;) __asm__ volatile("hlt");
}

void destroy_process(uint64_t pid) {
    for (int i = 0; i < process_count; i++) {
        if (process_table[i] && process_table[i]->pid == pid) {
            kfree(process_table[i]->stack);
            kfree(process_table[i]);
            process_table[i] = process_table[--process_count];
            return;
        }
    }
}

process_t* find_process(uint64_t pid) {
    for (int i = 0; i < process_count; i++) {
        if (process_table[i] && process_table[i]->pid == pid)
            return process_table[i];
    }
    return NULL;
}

process_t* get_current_process(void) {
    if (process_count > 0 && current_idx < process_count)
        return process_table[current_idx];
    return NULL;
}

// Global variables for assembly-level context switching
uint64_t saved_rsp = 0;
uint64_t next_rsp = 0;
uint64_t next_cr3 = 0;

// Called from the IRQ stub after EOI, with saved_rsp set.
void irq_scheduler_tick(void) {
    // DISABLED: always stay in current context
    next_rsp = saved_rsp;
    next_cr3 = (uint64_t)kernel_pml4_phys;
}

void schedule(void) {
}

static void idle_task(void) {
    while (1) {
        __asm__ volatile("hlt");
    }
}

static int idle_created = 0;

void ensure_idle_process(void) {
    if (idle_created) return;
    idle_created = 1;
    create_process("idle", (void*)idle_task, 0);
}

static void task_blink(void) {
    outb(0x3F8, '.');
}

static int task_uptime_counter = 0;
static void task_uptime(void) {
    task_uptime_counter++;
    if (task_uptime_counter >= 50) {
        task_uptime_counter = 0;
    }
}

typedef struct {
    char name[16];
    void (*func)(void);
    int active;
} background_task_t;

static background_task_t bg_tasks[8];
static int bg_task_count = 0;
static int bg_task_cur = 0;

void register_background_task(const char* name, void (*func)(void)) {
    if (bg_task_count >= 8) return;
    strncpy(bg_tasks[bg_task_count].name, name, 15);
    bg_tasks[bg_task_count].func = func;
    bg_tasks[bg_task_count].active = 1;
    bg_task_count++;
}

void run_background_tasks(void) {
    if (bg_task_count == 0) return;
    bg_task_cur = (bg_task_cur + 1) % bg_task_count;
    if (bg_tasks[bg_task_cur].active)
        bg_tasks[bg_task_cur].func();
}

void init_background_tasks(void) {
    register_background_task("blink", task_blink);
    register_background_task("uptime", task_uptime);
}

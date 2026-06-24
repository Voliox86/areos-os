#include "kernel.h"

extern process_t* process_table[MAX_PROCESSES];
extern int process_count;
static uint32_t next_pid = 1;

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
        init->page_directory = (void*)get_kernel_page_directory();
        process_table[process_count++] = init;
    }
}

// Set up a kernel stack for a new process so irq_scheduler_tick can
// switch to it. Stack layout (from low to high addr):
//   pusha regs (8 dwords), error(0), int_no, EIP, CS, EFLAGS
// When the ISR stub does popa + add esp,8 + iret on this stack,
// execution jumps to entry_point.
static void init_task_stack(process_t* proc, void* entry_point) {
    void* stack_mem = kmalloc(4096);
    if (!stack_mem) return;
    uint32_t* sp = (uint32_t*)((uint32_t)stack_mem + 4096);

    // CPU-pushed frame (iret pops these)
    *--sp = 0x202;          // EFLAGS (IF set)
    *--sp = 0x08;           // CS = kernel code segment
    *--sp = (uint32_t)entry_point; // EIP

    // ISR stub pushes: error code = 0, int number = 32
    *--sp = 32;             // int number (irq0)
    *--sp = 0;              // error code

    // pusha (order: eax, ecx, edx, ebx, old_esp, ebp, esi, edi)
    *--sp = 0;  // edi
    *--sp = 0;  // esi
    *--sp = 0;  // ebp
    *--sp = 0;  // old_esp (unused)
    *--sp = 0;  // ebx
    *--sp = 0;  // ecx
    *--sp = 0;  // edx
    *--sp = 0;  // eax

    proc->stack = (void*)sp;
    proc->kernel_stack = (void*)((uint32_t)stack_mem + 4096);
}

// Set up a stack for a ring-3 user process. When the ISR stub pops this frame
// and executes iret with CS=USER_CS (ring 3), the CPU will also pop SS and ESP
// from the stack, transitioning to user mode.
static void init_user_task_stack(process_t* proc, void* entry_point, void* user_stack_top) {
    void* stack_mem = kmalloc(4096);
    if (!stack_mem) return;
    uint32_t* sp = (uint32_t*)((uint32_t)stack_mem + 4096);

    // iret pops these when switching to ring 3 (SS:ESP are extra for ring transition)
    *--sp = 0x23;               // SS = user data segment (ring 3)
    *--sp = (uint32_t)user_stack_top; // ESP (user stack top)
    *--sp = 0x200;              // EFLAGS (IF set, IOPL=0)
    *--sp = 0x1B;               // CS = user code segment (ring 3)
    *--sp = (uint32_t)entry_point; // EIP

    // ISR stub pushes: error code = 0, int number = 32
    *--sp = 32;                 // int number (irq0)
    *--sp = 0;                  // error code

    // pusha
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;

    proc->stack = (void*)sp;
    proc->kernel_stack = (void*)((uint32_t)stack_mem + 4096);
}

process_t* create_process(const char* name, void* entry, uint32_t flags) {
    if (process_count >= MAX_PROCESSES) return NULL;
    process_t* p = (process_t*)kmalloc(sizeof(process_t));
    if (!p) return NULL;
    memset_asm(p, 0, sizeof(process_t));
    p->pid = next_pid++;
    p->ppid = 0;
    p->state = 1;
    p->stealth_level = (flags & 0x1) ? 5 : 0;
    strncpy(p->comm, name, 31);
    p->comm[31] = '\0';
    if (entry) {
        init_task_stack(p, entry);
    }
    process_table[process_count++] = p;
    return p;
}

process_t* create_user_process(const char* name, void* entry, void* user_stack, uint32_t* page_dir) {
    if (process_count >= MAX_PROCESSES) return NULL;
    process_t* p = (process_t*)kmalloc(sizeof(process_t));
    if (!p) return NULL;
    memset_asm(p, 0, sizeof(process_t));
    p->pid = next_pid++;
    p->state = 1;
    p->page_directory = page_dir;
    strncpy(p->comm, name, 31);
    p->comm[31] = '\0';

    // Allocate user stack pages if not provided
    if (!user_stack) {
        void* stack_page = alloc_page();
        if (!stack_page) { kfree(p); return NULL; }
        // Map user stack at high address (below framebuffer at 0xE0000000)
        uint32_t stack_virt = 0xD0000000 - 4096;
        map_page_dir(page_dir, stack_page, (void*)stack_virt, 0x7);
        user_stack = (void*)(stack_virt + 4096);  // stack top
    }

    init_user_task_stack(p, entry, user_stack);
    process_table[process_count++] = p;
    return p;
}

void switch_to_user_process(process_t* proc) {
    if (!proc || !proc->page_directory) return;
    switch_page_directory((uint32_t*)proc->page_directory);
    tss_set_stack((uint32_t)proc->kernel_stack);
}

void destroy_process(uint32_t pid) {
    for (int i = 0; i < process_count; i++) {
        if (process_table[i] && process_table[i]->pid == pid) {
            kfree(process_table[i]);
            process_table[i] = NULL;
            return;
        }
    }
}

process_t* find_process(uint32_t pid) {
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

void hide_process(uint32_t pid) {
    process_t* p = find_process(pid);
    if (p) p->stealth_level = 5;
}

void unhide_process(uint32_t pid) {
    process_t* p = find_process(pid);
    if (p) p->stealth_level = 0;
}

// Global variables for assembly-level context switching
uint32_t saved_esp = 0;
uint32_t next_esp = 0;

// Called from the IRQ stub after EOI, with saved_esp set.
// Determines the next process to run and sets next_esp.
void irq_scheduler_tick(void) {
    if (process_count < 2) {
        next_esp = saved_esp;
        return;
    }

    static int tick_counter = 0;
    tick_counter++;
    if (tick_counter < 5) {
        next_esp = saved_esp;
        return;
    }
    tick_counter = 0;

    // Save current process's stack pointer
    process_t* current = process_table[current_idx];
    if (current) {
        current->stack = (void*)saved_esp;
    }

    // Find next runnable process (round-robin)
    int next = current_idx;
    for (int i = 0; i < process_count; i++) {
        next = (next + 1) % process_count;
        if (process_table[next] && process_table[next]->state)
            break;
    }

    if (next != current_idx) {
        current_idx = next;
        process_t* next_proc = process_table[next];
        if (next_proc && next_proc->stack) {
            // Switch page directory if the process has its own
            if (next_proc->page_directory) {
                switch_page_directory((uint32_t*)next_proc->page_directory);
                tss_set_stack((uint32_t)next_proc->kernel_stack);
            }
            next_esp = (uint32_t)next_proc->stack;
            return;
        }
    }
    next_esp = saved_esp;
}

void schedule(void) {
}

// Idle task that halts the CPU when nothing else is running
static void idle_task(void) {
    while (1) {
        __asm__ volatile("hlt");
    }
}

static int idle_created = 0;

void ensure_idle_process(void) {
    if (idle_created) return;
    idle_created = 1;
    create_process("idle", idle_task, 0);
}

// Background tasks
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

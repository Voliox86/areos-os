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

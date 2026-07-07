#include "kernel.h"

/* ============================================================
 * mmap.c — anonymous, demand-zero memory mappings (v5.8.12)
 * ============================================================
 * A process reserves a virtual range with mmap(); the pages materialise on first
 * touch. do_mmap only records a VMA (start/end/prot) and bumps a per-process
 * pointer — no pages are allocated. A fault inside a VMA is serviced by
 * vm_handle_fault (paging.c), which allocs a zeroed page and maps it with the
 * VMA's prot. munmap frees the present pages (refcount-aware) and drops the VMA.
 *
 * Mappings sit in [MMAP_BASE, MMAP_MAX) = [4 GiB, 112 TiB): above the 4 GiB heap
 * cap (SYS_SBRK) and far below the 128 TiB user stack, so they never collide. This
 * is anonymous MAP_PRIVATE only — file-backed mmap and addr hints are future work. */

/* Find the VMA containing `addr`, or NULL. Called from the #PF handler. */
vma_t* vma_find(process_t* p, uint64_t addr) {
    if (!p) return 0;
    for (int i = 0; i < PROC_MAX_VMAS; i++) {
        vma_t* v = &p->mmap_vmas[i];
        if (v->used && addr >= v->start && addr < v->end) return v;
    }
    return 0;
}

/* SYS_MMAP: reserve `length` bytes of anonymous, demand-zero memory. addr is an
 * ignored hint; fd/offset are ignored (anonymous). Returns the base VA, or
 * MAP_FAILED ((uint64_t)-1) if out of VMA slots or address space. */
uint64_t do_mmap(uint64_t addr, uint64_t length, int prot, int flags) {
    (void)addr; (void)flags;                            /* v1: anonymous private only */
    process_t* p = get_current_process();
    if (!p || length == 0) return (uint64_t)-1;
    length = (length + 0xFFF) & ~0xFFFULL;              /* round up to whole pages */
    if (p->mmap_next < MMAP_BASE) p->mmap_next = MMAP_BASE;   /* lazy first-use init */

    int slot = -1;
    for (int i = 0; i < PROC_MAX_VMAS; i++)
        if (!p->mmap_vmas[i].used) { slot = i; break; }
    if (slot < 0) return (uint64_t)-1;                  /* too many regions */

    uint64_t base = p->mmap_next;
    if (base + length > MMAP_MAX || base + length < base) return (uint64_t)-1;
    p->mmap_next = base + length;

    p->mmap_vmas[slot].start = base;
    p->mmap_vmas[slot].end   = base + length;
    p->mmap_vmas[slot].prot  = (uint32_t)prot;
    p->mmap_vmas[slot].used  = 1;
    return base;                                        /* pages fault in on first touch */
}

/* SYS_MUNMAP: release [addr, addr+length). Frees every present page in the range
 * (free_page is refcount-aware, so a COW-shared page just drops a reference) and
 * drops any VMA fully covered by the range. Partial unmaps (splitting a VMA) are
 * not supported in v1 — the pages are freed but the VMA record is left intact. */
int do_munmap(uint64_t addr, uint64_t length) {
    extern void vm_free_range(uint64_t* pml4, uint64_t start, uint64_t end);
    process_t* p = get_current_process();
    if (!p || !p->page_directory || length == 0) return -1;
    addr &= ~0xFFFULL;
    length = (length + 0xFFF) & ~0xFFFULL;
    uint64_t end = addr + length;

    vm_free_range((uint64_t*)p->page_directory, addr, end);

    for (int i = 0; i < PROC_MAX_VMAS; i++) {
        vma_t* v = &p->mmap_vmas[i];
        if (v->used && v->start >= addr && v->end <= end) v->used = 0;
    }
    return 0;
}

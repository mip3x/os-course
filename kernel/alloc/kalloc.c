// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/hw/memlayout.h"
#include "kernel/locking/spinlock.h"
#include "kernel/hw/riscv.h"
#include "kernel/defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

// array of reference count of the number of
// user page tables that refer to page idx
int refcount[(PHYSTOP - KERNBASE) / PGSIZE];
struct spinlock refcount_lock;

void inc_refcount(void *pa) {
    acquire(&refcount_lock);
    refcount[REFIDX(pa)]++;
    release(&refcount_lock);
}

void kinit() {
    bd_init((void*)PGROUNDUP((uint64)end), (void*)PHYSTOP);
    initlock(&refcount_lock, "refcount_lock");
}

void freerange(void *pa_start, void *pa_end) {
    char *p;
    p = (char *)PGROUNDUP((uint64)pa_start);
    for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
        kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {
    if (bd_blk_size(pa) == PGSIZE) {
        acquire(&refcount_lock);
        if (--refcount[REFIDX(pa)] <= 0)
            bd_free(pa);
        release(&refcount_lock);
    } else {
        bd_free(pa);
    }
}

// Allocate arbitrary number of bytes of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(uint64 nbytes) {
    void *pa = bd_malloc(nbytes);
    if (!pa)
        return pa;
    if (nbytes == PGSIZE)
        inc_refcount(pa);
    return pa;
}

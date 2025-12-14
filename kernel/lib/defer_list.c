#include "kernel/types.h"
#include "kernel/lib/list.h"
#include "kernel/param.h"
#include "kernel/hw/memlayout.h"
#include "kernel/hw/riscv.h"
#include "kernel/locking/spinlock.h"
#include "kernel/proc/proc.h"
#include "kernel/lib/defer_list.h"
#include "kernel/defs.h"

// Initialize defer domain
void defer_init(struct defer_domain *d, char *name) {
    initlock(&d->lock, name);
    d->walkers = 0;
    lst_init(&d->deferred_head);
}

// Enter critical section
void defer_enter(struct defer_domain *d) {
    acquire(&d->lock);
    d->walkers++;
    release(&d->lock);
}

// Exit critical section
void defer_exit(struct defer_domain *d) {
    acquire(&d->lock);
    d->walkers--;
    release(&d->lock);
}

// Connect ptr with defer_node and push to defer list defer_node to free (release) later
void defer_ptr(struct defer_domain *d, void *ptr) {
    struct defer_node *node = (struct defer_node*)kalloc(sizeof(struct defer_node));
    if (node == 0)
        return;
    node->ptr = ptr;

    acquire(&d->lock);
    lst_push(&d->deferred_head, &node->link);
    release(&d->lock);
}

// Free (release) all objects that are not required by anybody
void defer_reclaim(struct defer_domain *d) {
    for (;;) {
        acquire(&d->lock);

        if (d->walkers != 0 || lst_empty(&d->deferred_head)) {
            release(&d->lock);
            return;
        }

        struct list *e = (struct list*)lst_pop(&d->deferred_head);
        release(&d->lock);

        struct defer_node *node = (struct defer_node*)e;
        kfree(node->ptr);
        kfree(node);
    }
}
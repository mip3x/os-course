#include "kernel/lib/list.h"

#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/hw/memlayout.h"
#include "kernel/locking/spinlock.h"
#include "kernel/hw/riscv.h"
#include "kernel/defs.h"

// double-linked, circular list. double-linked makes remove
// fast. circular simplifies code, because don't have to check for
// empty list in insert and remove.

void lst_init(struct list *lst) {
    lst->next = lst;
    lst->prev = lst;

    lst->size = 0;
}

int lst_empty(struct list *lst) { return lst->next == lst; }

void lst_remove(struct list *lst, struct list *e) {
    e->prev->next = e->next;
    e->next->prev = e->prev;

    lst->size--;
}

void *lst_pop(struct list *lst) {
    if (lst->next == lst)
        panic("lst_pop");
    struct list *p = lst->next;
    lst_remove(lst, p);
    return (void *)p;
}

void lst_push(struct list *lst, void *p) {
    struct list *e = (struct list *)p;
    e->next = lst->next;
    e->prev = lst;
    lst->next->prev = p;
    lst->next = e;

    lst->size++;
}

void lst_print(struct list *lst) {
    for (struct list *p = lst->next; p != lst; p = p->next) {
        printf(" %p", p);
    }
    printf("\n");
}

// Sleeping locks

#include "kernel/types.h"
#include "kernel/hw/riscv.h"
#include "kernel/defs.h"
#include "kernel/param.h"
#include "kernel/hw/memlayout.h"
#include "kernel/locking/spinlock.h"
#include "kernel/proc/proc.h"
#include "kernel/locking/sleeplock.h"

void initsleeplock(struct sleeplock *lk, char *name) {
    initlock(&lk->lk, "sleep lock");
    lk->name = name;
    lk->locked = 0;
    lk->pid = 0;
}

void acquiresleep(struct sleeplock *lk) {
    acquire(&lk->lk);
    while (lk->locked) {
        sleep(lk, &lk->lk);
    }
    lk->locked = 1;
    lk->pid = myproc()->pid;
    release(&lk->lk);
}

void releasesleep(struct sleeplock *lk) {
    acquire(&lk->lk);
    lk->locked = 0;
    lk->pid = 0;
    wakeup(lk);
    release(&lk->lk);
}

int holdingsleep(struct sleeplock *lk) {
    int r;

    acquire(&lk->lk);
    r = lk->locked && (lk->pid == myproc()->pid);
    release(&lk->lk);
    return r;
}

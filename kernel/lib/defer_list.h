#pragma once

#include "kernel/locking/spinlock.h"
#include "list.h"

struct defer_domain {
    struct spinlock lock;
    struct list deferred_head;
    int walkers;
};

struct defer_node {
    struct list link;
    void *ptr;
};
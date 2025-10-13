#pragma once

#include "kernel/types.h"
#include "kernel/locking/sleeplock.h"
#include "kernel/file/fs.h"

struct buf {
    int valid; // has data been read from disk?
    int disk;  // does disk "own" buf?
    uint dev;
    uint blockno;
    struct sleeplock lock;
    uint refcnt;
    struct buf *prev; // LRU cache list
    struct buf *next;
    uchar data[BSIZE];
};

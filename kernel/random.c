#include "kernel/types.h"
#include "kernel/defs.h"
#include "kernel/locking/spinlock.h"

// Random Number Generation seed
uint64 kernel_seed;
struct spinlock kernel_seed_lock;

void randominit(void) {
    initlock(&kernel_seed_lock, "kernel_seed");
}

// https://rosettacode.org/wiki/Pseudo-random_numbers/Splitmix64
static uint64 splitmix64(uint64 *state) {
    uint64 z = (*state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static uint64 next_random(uint64 start, uint64 end) {
    acquire(&kernel_seed_lock);
    uint64 prng_algo_result = splitmix64(&kernel_seed);
    release(&kernel_seed_lock);

    return (prng_algo_result % (end - start)) + start;
}

uint64 get_random(uint64 start, uint64 end) {
    if (end <= start)
        panic("get_random: incorrect ranges");

    return next_random(start, end);
}
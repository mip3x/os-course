//
// driver for qemu's virtio rng device.
// uses qemu's mmio interface to virtio.
//
// qemu ... -object rng-random,filename=/dev/urandom,id=rng0 -device
// virtio-rng-device,rng=rng0,bus=virtio-mmio-bus.1
//

#include "kernel/types.h"
#include "kernel/defs.h"
#include "kernel/param.h"
#include "kernel/hw/memlayout.h"
#include "kernel/locking/spinlock.h"
#include "kernel/virt/virtio.h"

// the address of virtio mmio register r.
#define R1(r) ((volatile uint32 *)(VIRTIO1 + (r)))

// https://elixir.bootlin.com/linux/v6.18.6/source/include/uapi/linux/virtio_ids.h#L35
#define VIRTIO_ID_RNG 4

struct virtio_rng_queue {
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    char is_free; // is a descriptor free?
};

static struct rng {
    struct virtio_rng_queue seed_queue;
    struct spinlock rng_lock;
    char is_request_done;
} rng;

// mark a descriptor as free
static void free_desc(int i) {
    if (i >= NUM)
        panic("free_desc 1");
    if (rng.seed_queue.is_free)
        panic("free_desc 2");

    rng.seed_queue.desc[i].addr = 0;
    rng.seed_queue.desc[i].len = 0;
    rng.seed_queue.desc[i].flags = 0;
    rng.seed_queue.desc[i].next = 0;
    rng.seed_queue.is_free = 1;
}

void virtio_rng_init(void) {
    uint32 status = 0;

    initlock(&rng.rng_lock, "virtio_rng");

    if (*R1(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
        *R1(VIRTIO_MMIO_VERSION) != 2 || *R1(VIRTIO_MMIO_DEVICE_ID) != VIRTIO_ID_RNG ||
        *R1(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551) {
        panic("could not find virtio rng");
    }

    // reset device
    *R1(VIRTIO_MMIO_STATUS) = status;

    // set ACKNOWLEDGE status bit
    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
    *R1(VIRTIO_MMIO_STATUS) = status;

    // set DRIVER status bit
    status |= VIRTIO_CONFIG_S_DRIVER;
    *R1(VIRTIO_MMIO_STATUS) = status;

    // negotiate features
    uint64 features = *R1(VIRTIO_MMIO_DEVICE_FEATURES);
    *R1(VIRTIO_MMIO_DRIVER_FEATURES) = features;

    // tell device that feature negotiation is complete.
    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    *R1(VIRTIO_MMIO_STATUS) = status;

    // re-read status to ensure FEATURES_OK is set.
    status = *R1(VIRTIO_MMIO_STATUS);
    if (!(status & VIRTIO_CONFIG_S_FEATURES_OK))
        panic("virtio rng FEATURES_OK unset");

    // initialize queue 0.
    *R1(VIRTIO_MMIO_QUEUE_SEL) = 0;

    // ensure queue 0 is not in use.
    if (*R1(VIRTIO_MMIO_QUEUE_READY))
        panic("virtio rng should not be ready");

    // check maximum queue size.
    uint32 max = *R1(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max == 0)
        panic("virtio rng has no queue 0");
    if (max < 1)
        panic("virtio rng max queue too short");

    // allocate and zero queue memory.
    rng.seed_queue.desc = kalloc(sizeof(struct virtq_desc));
    rng.seed_queue.avail = kalloc(sizeof(struct virtq_avail));
    rng.seed_queue.used = kalloc(sizeof(struct virtq_used));
    if (!rng.seed_queue.desc || !rng.seed_queue.avail || !rng.seed_queue.used)
        panic("virtio rng kalloc");
    memset(rng.seed_queue.desc, 0, sizeof(struct virtq_desc));
    memset(rng.seed_queue.avail, 0, sizeof(struct virtq_avail));
    memset(rng.seed_queue.used, 0, sizeof(struct virtq_used));

    // set queue size.
    *R1(VIRTIO_MMIO_QUEUE_NUM) = 0x1;

    // write physical addresses.
    *R1(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)rng.seed_queue.desc;
    *R1(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)rng.seed_queue.desc >> 32;
    *R1(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)rng.seed_queue.avail;
    *R1(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)rng.seed_queue.avail >> 32;
    *R1(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)rng.seed_queue.used;
    *R1(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)rng.seed_queue.used >> 32;

    // queue is ready.
    *R1(VIRTIO_MMIO_QUEUE_READY) = 0x1;

    // all NUM descriptors start out unused.
    rng.seed_queue.is_free = 1;

    // tell device we're completely ready.
    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    *R1(VIRTIO_MMIO_STATUS) = status;

    // plic.c and trap.c arrange for interrupts from VIRTIO0_IRQ.
}

void virtio_rng_request_seed(void) {
    void *buf = kalloc(sizeof(uint64));
    if (!buf)
        panic("virtio rng kalloc buf");
    memset(buf, 0, sizeof(uint64));

    acquire(&rng.rng_lock);

    // the only one descriptor
    int idx = 0;

    // mark seed_queue busy
    if (rng.seed_queue.is_free != 1) {
        while (rng.seed_queue.is_free != 1) {
            __sync_synchronize();
        }
    }
    rng.seed_queue.is_free = 0;

    struct virtq_desc *desc = &rng.seed_queue.desc[idx];
    desc->addr = (uint64)buf;
    desc->len = sizeof(uint64);
    desc->flags = VRING_DESC_F_WRITE;
    desc->next = 0;

    rng.seed_queue.avail->ring[rng.seed_queue.avail->idx] = idx;
    __sync_synchronize();

    rng.seed_queue.avail->idx++;
    __sync_synchronize();

    *R1(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; // value is queue number

    // Wait for device to say request is finished 
    while (rng.seed_queue.used->idx == 0) {
        __sync_synchronize();
    }

    // assign to variable buf value
    acquire(&kernel_seed_lock);
    kernel_seed = *((uint64*)buf);
    release(&kernel_seed_lock);

#if KDEBUG == 1
    acquire(&kernel_seed_lock);
    printf("virtio_rng_request_seed: kernel_seed = 0x%lx\n", kernel_seed);
    release(&kernel_seed_lock);
#endif

    free_desc(0);
    kfree(buf);

    release(&rng.rng_lock);
}
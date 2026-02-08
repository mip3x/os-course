//
// driver for qemu's virtio rng device.
// uses qemu's mmio interface to virtio.
//
// qemu ... -object rng-random,filename=/dev/urandom,id=rng0 -device
// virtio-rng-device,rng=rng0,bus=virtio-mmio-bus.1
//

#include "kernel/types.h"
#include "kernel/hw/riscv.h"
#include "kernel/defs.h"
#include "kernel/param.h"
#include "kernel/hw/memlayout.h"
#include "kernel/locking/spinlock.h"
#include "kernel/locking/sleeplock.h"
#include "kernel/file/fs.h"
#include "kernel/file/buf.h"
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
    uint64 data;
} rng;

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
    if (max < NUM)
        panic("virtio rng max queue too short");

    // allocate and zero queue memory.
    rng.seed_queue.desc = kalloc(PGSIZE);
    rng.seed_queue.avail = kalloc(PGSIZE);
    rng.seed_queue.used = kalloc(PGSIZE);
    if (!rng.seed_queue.desc || !rng.seed_queue.avail || !rng.seed_queue.used)
        panic("virtio rng kalloc");
    memset(rng.seed_queue.desc, 0, PGSIZE);
    memset(rng.seed_queue.avail, 0, PGSIZE);
    memset(rng.seed_queue.used, 0, PGSIZE);

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

void virtio_rng_intr() {
    acquire(&rng.rng_lock);
    release(&rng.rng_lock);
}
#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/hw/memlayout.h"
#include "kernel/hw/riscv.h"
#include "kernel/locking/spinlock.h"
#include "kernel/proc/proc.h"
#include "kernel/defs.h"
#include "kernel/elf.h"

static int loadseg(pde_t *, uint64, struct inode *, uint, uint);

int flags2perm(int flags) {
    int perm = 0;
    if (flags & 0x1)
        perm = PTE_X;
    if (flags & 0x2)
        perm |= PTE_W;
    return perm;
}

int exec(char *path, char **argv) {
    char *s, *last;
    int i, off;
    uint64 argc, sz = 0, sp, ustack[MAXARG], stackbase;
    struct elfhdr elf;
    struct inode *ip;
    struct proghdr ph;
    pagetable_t pagetable = 0, oldpagetable;
    struct proc *p = myproc();

    begin_op();

    // get randomize_va_space flag
    uint8 randomize_va_space = 1; // TODO make file-flag

#if KDEBUG == 1
    // print out binary name & randomize_va_space flag
    printf("exec %s\n", path);
    printf("aslr: %d\n", randomize_va_space);
#endif

    if ((ip = namei(path)) == 0) {
        end_op();
        return -1;
    }
    ilock(ip);

    // Check ELF header
    if (readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
        goto bad;

    if (elf.magic != ELF_MAGIC)
        goto bad;

    if ((pagetable = proc_pagetable(p)) == 0)
        goto bad;

    // Load program into memory.
    for (i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph)) {
        if (readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
            goto bad;

        // _init example:
        // e.g. ph.vaddr = 0x2ee8
        uint64 va0 = PGROUNDDOWN(ph.vaddr);     // va0 = 0x2000
        uint64 pageoff = ph.vaddr - va0;        // pageoff = 0xee8
        uint64 offset0 = ph.off - pageoff;      // offset0 = 0x1000
        uint64 filesz0 = ph.filesz + pageoff;   // filesz0 = 0x128 + 0xee8

        if (ph.type != ELF_PROG_LOAD)
            continue;
        if (ph.memsz < ph.filesz)
            goto bad;
        if (ph.vaddr + ph.memsz < ph.vaddr)
            goto bad;
        if (ph.off < pageoff)
            goto bad;
        uint64 sz1;
        if ((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz,
                            flags2perm(ph.flags))) == 0)
            goto bad;
        sz = sz1;
        // loadseg argument 'va' must be page-aligned
        //   it means we load more bytes operating with pages so
        //   that ph.vaddr access will be to the same virtual address
        //   as it was supposed when creating ELF
        if (loadseg(pagetable, va0, ip, offset0, filesz0) < 0)
            goto bad;
    }
    iunlockput(ip);
    end_op();
    ip = 0;

    p = myproc();
    uint64 oldsz = p->sz;

    // Allocate random number of pages at the next page boundary.
    // Make the last - 1 inaccessible as a stack guard.
    // Use the last as the user stack.
    sz = PGROUNDUP(sz);
    // page-level randomization from 1 to 256 pages
    uint64 stack_offset = randomize_va_space ? get_random(1, 257) : 0;
    uint64 sz1;
    if ((sz1 = uvmalloc(pagetable, sz, sz + (stack_offset + USERSTACK + 1) * PGSIZE, PTE_W)) ==
        0)
        goto bad;
    sz = sz1;
    uvmclear(pagetable, sz - (USERSTACK + 1) * PGSIZE);
    sp = sz;
    stackbase = sp - USERSTACK * PGSIZE;

    // Push argument strings, prepare rest of stack in ustack.
    for (argc = 0; argv[argc]; argc++) {
        if (argc >= MAXARG)
            goto bad;
        sp -= strlen(argv[argc]) + 1;
        sp -= sp % 16; // riscv sp must be 16-byte aligned
        if (sp < stackbase)
            goto bad;
        if (copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
            goto bad;
        ustack[argc] = sp;
    }
    ustack[argc] = 0;

    // push the array of argv[] pointers.
    sp -= (argc + 1) * sizeof(uint64);
    sp -= sp % 16;
    if (sp < stackbase)
        goto bad;
    if (copyout(pagetable, sp, (char *)ustack, (argc + 1) * sizeof(uint64)) < 0)
        goto bad;

    // arguments to user main(argc, argv)
    // argc is returned via the system call return
    // value, which goes in a0.
    p->trapframe->a1 = sp;

    // Save program name for debugging.
    for (last = s = path; *s; s++)
        if (*s == '/')
            last = s + 1;
    safestrcpy(p->name, last, sizeof(p->name));

    // Commit to the user image.
    oldpagetable = p->pagetable;
    p->pagetable = pagetable;
    p->sz = sz;
    p->trapframe->epc = elf.entry; // initial program counter = main
    p->trapframe->sp = sp;         // initial stack pointer
    proc_freepagetable(oldpagetable, oldsz);

    return argc; // this ends up in a0, the first argument to main(argc, argv)

bad:
    if (pagetable)
        proc_freepagetable(pagetable, sz);
    if (ip) {
        iunlockput(ip);
        end_op();
    }
    return -1;
}

// Load a program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int loadseg(pagetable_t pagetable, uint64 va, struct inode *ip,
                   uint offset, uint sz) {
    uint i, n;
    uint64 pa;

    for (i = 0; i < sz; i += PGSIZE) {
        pa = walkaddr(pagetable, va + i);
        if (pa == 0)
            panic("loadseg: address should exist");
        if (sz - i < PGSIZE)
            n = sz - i;
        else
            n = PGSIZE;
        if (readi(ip, 0, (uint64)pa, offset + i, n) != n)
            return -1;
    }

    return 0;
}

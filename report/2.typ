#set page(
  paper: "a4",
  margin: (left: 25mm, right: 15mm, top: 20mm, bottom: 20mm),
)

#set text(
  font: "JetBrains Mono",
  size: 11pt,
)

#set par(
  leading: 1.4em,
  justify: true,
)

#set heading(numbering: "1.")

#let organization = "ФГБОУ высшего профессионального образования «Санкт-Петербургский национальный исследовательский университет информационных технологий, механики и оптики»"
#let faculty = "Факультет программной инженерии и компьютерной техники (ФПИиКТ)"

#let work_title = "Лабораторная работа №2"
#let work_name = "Аллокатор"

#let student_name = "Малышев Михаил Александрович"
#let group = "Группа P3311"

#let teacher_name = "Смирнов Виктор Игоревич"

#let year = "2025"

#align(center)[
  #v(10mm)
  #text(weight: "bold")[#organization]
  #v(3mm)
  #faculty
  #v(25mm)

  #text(weight: "bold", size: 16pt)[Отчёт по лабораторной работе]
  #v(6mm)
  #text(weight: "bold")[#work_title]
  #v(2mm)
  #work_name
]

#v(30mm)

#align(right)[
  Выполнил: #student_name \
  #group \
  \
  Преподаватель практики: #teacher_name
]

#v(25mm)

#align(center)[
  #year
]

#pagebreak()

= Ход работы

*Ссылка на Pull Request'ы:*
1. #link("https://github.com/mip3x/os-course/pull/3")
2. #link("https://github.com/mip3x/os-course/pull/4")

В ходе лабораторной работы были выполнены следующие шаги:

1. Подключён и инициализирован buddy allocator (файл `kernel/alloc/buddy.c`) как основной аллокатор физической памяти вместо страничного аллокатора
2. Изменён базовый аллокатор `kernel/alloc/kalloc.c`:
   - в `kinit()` выполнен вызов `bd_init()` с границами управляемой памяти;
   - `kalloc()` переопределён на `bd_malloc(nbytes)`;
   - `kfree()` переопределён на `bd_free(pa)`
3. Переведено выделение файловых структур со статического массива на динамическое выделение:
   - в `kernel/file/file.c` удалён/перестал использоваться статический массив структур файлов;
   - в `kernel/proc/proc.c` удалён/перестал использоваться статический массив структур процессов

= Промежуточные результаты

== Часть 1. Использование аллокатора

=== `kalloc.c`

В `kernel/alloc/kalloc.c` операции выделения/освобождения памяти перенаправлены на `buddy allocator`

Ключевое: `kinit()` инициализирует аллокатор на диапазоне `[PGROUNDUP(end), PHYSTOP)`, после чего выделения выполняются через `bd_malloc`, освобождение -- через `bd_free`

==== Код

*Листинг `kernel/alloc/kalloc.c`*

```c
void kinit() {
    bd_init((void*)PGROUNDUP((uint64)end), (void*)PHYSTOP);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {
    bd_free(pa);
}

// Allocate arbitrary number of bytes of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(uint64 nbytes) {
    return bd_malloc(nbytes);
}
```

=== `file.c`

Ключевые изменения:

- `filealloc()` теперь выделяет память под структуру файла вызовом `kalloc(sizeof(struct file))`.
- `fileclose()` освобождает память при завершении использования структуры (в момент, когда структура перестаёт быть нужна), через `bd_free(f)`

Блокировка в файловой подсистеме остаётся необходимой, так как операции выдачи/закрытия файла выполняются конкурентно, и требуется защищать общий доступ к структурам/счётчикам

==== Код

*Листинг `kernel/file/file.c`*

```c
// filealloc(): вместо поиска свободного слота в статическом массиве выделяется новая структура
struct file *f = (struct file *)kalloc(sizeof(struct file));

// fileclose(): при завершении использования структуры -- освобождение:
bd_free(f);
```

== Часть 2. Оптимизация buddy allocator

Оптимизация направлена на уменьшение метаданных аллокатора: вместо отдельного бита `занят/свободен` для каждого блока хранится один бит на пару соседних блоков: A занято XOR B занято

Свойство:

бит равен 1, если занят ровно один блок из пары;

бит равен 0, если оба свободны или оба заняты;

при выделении/освобождении достаточно инвертировать соответствующий бит

В логике освобождения это используется для решения, можно ли объединять блоки: если после освобождения бит пары становится 0, то оба `buddy` блока свободны и их можно объединить.

=== Код

*Листинг `kernel/alloc/buddy.c`*

```c
// * xor_alloc optimization part *
// returns pair index for block
// e.g. for blocks number 0 or 1 we need bit 0
int get_pair_index(int index) {
    return index / 2;
}

// invert pair bit in xor_alloc array
void pair_bit_invert(char *array, int index) {
    int pair_index = get_pair_index(index);
    bit_invert(array, pair_index);
}

// Free memory pointed to by p, which was earlier allocated using
// bd_malloc.
void bd_free(void *p) {
    void *q;
    int k;

    acquire(&lock);
    for (k = size(p); k < MAXSIZE; k++) {
        int bi = blk_index(k, p);
        int buddy = (bi % 2 == 0) ? bi + 1 : bi - 1;
        pair_bit_invert(bd_sizes[k].xor_alloc, bi);         // free p at size k

        int pair_buddy = get_pair_index(buddy);
        if (bit_isset(bd_sizes[k].xor_alloc, pair_buddy)) { // is buddy allocated?
            break;                                 // break out of loop
        }
        // budy is free; merge with buddy
        q = addr(k, buddy);
        lst_remove(&bd_sizes[k].free, q); // remove buddy from free list
        if (buddy % 2 == 0) {
            p = q;
        }
        // at size k+1, mark that the merged buddy pair isn't split
        // anymore
        bit_clear(bd_sizes[k + 1].split, blk_index(k + 1, p));
    }
    lst_push(&bd_sizes[k].free, p);
    release(&lock);
}

// allocate nbytes, but malloc won't return anything smaller than LEAF_SIZE
void *bd_malloc(uint64 nbytes) {
    int fk, k;

    acquire(&lock);

    // Find a free block >= nbytes, starting with smallest k possible
    fk = firstk(nbytes);
    for (k = fk; k < nsizes; k++) {
        if (!lst_empty(&bd_sizes[k].free))
            break;
    }
    if (k >= nsizes) { // No free blocks?
        release(&lock);
        return 0;
    }

    // Found a block; pop it and potentially split it.
    char *p = lst_pop(&bd_sizes[k].free);
    pair_bit_invert(bd_sizes[k].xor_alloc, blk_index(k, p));
    for (; k > fk; k--) {
        // split a block at size k and mark one half allocated at size k-1
        // and put the buddy on the free list at size k-1
        char *q = p + BLK_SIZE(k - 1); // p's buddy
        bit_set(bd_sizes[k].split, blk_index(k, p));
        pair_bit_invert(bd_sizes[k - 1].xor_alloc, blk_index(k - 1, p));
        lst_push(&bd_sizes[k - 1].free, q);
    }
    release(&lock);

    return p;
}

// часть bd_init(void *base, void *end)
// initialize free list and allocate the alloc array for each size k
for (int k = 0; k < nsizes; k++) {
    lst_init(&bd_sizes[k].free);

    // xor_alloc is calculated for 2 blocks
    sz = sizeof(char) * (ROUNDUP(NBLK(k), 8)) / 8;
    if (sz / 2 == 0)
        sz = 1;
    else
        sz /= 2;

    bd_sizes[k].xor_alloc = p;
    memset(bd_sizes[k].xor_alloc, 0, sz);
    p += sz;
}

// allocate the split array for each size k, except for k = 0, since
// we will not split blocks of size k = 0, the smallest size.
for (int k = 1; k < nsizes; k++) {
    sz = sizeof(char) * (ROUNDUP(NBLK(k), 8)) / 8;
    bd_sizes[k].split = p;
    memset(bd_sizes[k].split, 0, sz);
    p += sz;
}
```

== Часть 3. Использование аллокатора для структур процессов

В 3-ей части лабораторной работы `buddy allocator` был применён для динамического управления объектами, связанными с процессами. В отличие от статического подхода (массив фиксированного размера), динамическое выделение позволяет:

- уменьшить статическое потребление памяти ядром;
- создавать и освобождать процессные структуры по мере необходимости;
- устранить жёсткую привязку к константе `NPROC` как к ограничителю по памяти

При динамическом управлении объектами процессов появляется важная проблема конкурентного доступа: в `xv6` несколько потоков выполнения (ядро на разных CPU) могут одновременно обходить список структур процессов и/или освобождать элементы. Чтобы не допустить `use-after-free`(`UAF`), был использован механизм отложенного освобождения (`deferred reclaim`)

Реализован домен отложенного освобождения:
- при входе в критическую секцию обхода увеличивается счётчик активных `walkers`;
- освобождение объектов вместо немедленного `kfree` помещает их в список `deferred`;
- освобождение реально выполняется только тогда, когда ни один поток не находится внутри обхода (`walkers` = 0)

Таким образом обеспечивается безопасное освобождение объектов даже при конкурентных обходах

=== Код

*Листинг — kernel/lib/defer_list.h*

```c
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
```

*Листинг `kernel/lib/defer_list.c`*

```c
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
int defer_ptr(struct defer_domain *d, void *ptr) {
    struct defer_node *node = (struct defer_node*)kalloc(sizeof(struct defer_node));
    if (node == 0)
        return -1;
    node->ptr = ptr;

    acquire(&d->lock);
    lst_push(&d->deferred_head, &node->link);
    release(&d->lock);

    return 0;
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
```

*Листинг `kernel/proc/proc.c` (основные изменения)*

```c
struct list proc_lst_head;
struct spinlock proc_lst_lock;

struct defer_domain proc_df;

// initialize the proc table.
void procinit(void) {
    initlock(&pid_lock, "nextpid");
    initlock(&wait_lock, "wait_lock");
    initlock(&proc_lst_lock, "proc_st_lock");

    acquire(&proc_lst_lock);
    lst_init(&proc_lst_head);
    release(&proc_lst_lock);

    defer_init(&proc_df, "proc_df");
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *allocproc(void) {
    struct proc *p = 0;

    struct list *iter;
    defer_enter(&proc_df);
    acquire(&proc_lst_lock);

    for (iter = proc_lst_head.next; iter != &proc_lst_head; iter = iter->next) {
        struct proc *candidate = (struct proc *)iter;

        acquire(&candidate->lock);
        if (candidate->state == UNUSED) {
            p = candidate;
            break;
        }
        release(&candidate->lock);
    }

    if (p == 0) { // candidate with UNUSED state not found -> alloc proc struct
        if (proc_lst_head.size >= NPROC) {
            release(&proc_lst_lock);
            defer_exit(&proc_df);
            return 0;
        }

        release(&proc_lst_lock);
        defer_exit(&proc_df);

        p = (struct proc *)kalloc(sizeof(struct proc));
        if (!p)
            return 0;

        memset(p, 0, sizeof(*p));

        initlock(&p->lock, "proc");
        acquire(&p->lock);

        acquire(&proc_lst_lock);
        lst_push(&proc_lst_head, &p->proc_lst_node);
        release(&proc_lst_lock);
    } else { // candidate with UNUSED state found
        release(&proc_lst_lock);
        defer_exit(&proc_df);
    }

    p->pid = allocpid();
    p->state = USED;

    // Allocate a trapframe page.
    if ((p->trapframe = (struct trapframe *)kalloc(PGSIZE)) == 0) {
        freeproc(p);
        release(&p->lock);
        return 0;
    }

    // An empty user page table.
    p->pagetable = proc_pagetable(p);
    if (p->pagetable == 0) {
        freeproc(p);
        release(&p->lock);
        return 0;
    }

    // A kernel stack.
    if ((p->kstack = (uint64)kalloc(PGSIZE)) == 0) {
        freeproc(p);
        release(&p->lock);
        return 0;
    }

    // Set up new context to start executing at forkret,
    // which returns to user space.
    memset(&p->context, 0, sizeof(p->context));
    p->context.ra = (uint64)forkret;
    p->context.sp = p->kstack + PGSIZE;

    return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void freeproc(struct proc *p) {
    if (p->trapframe)
        kfree((void *)p->trapframe);
    p->trapframe = 0;
    if (p->pagetable)
        proc_freepagetable(p->pagetable, p->sz);
    if (p->kstack)
        kfree((void*)p->kstack);

    p->pagetable = 0;
    p->sz = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->chan = 0;
    p->killed = 0;
    p->xstate = 0;
    p->state = UNUSED;
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr) {
    struct list *iter;
    int havekids, pid;
    struct proc *p = myproc();

    acquire(&wait_lock);

    for (;;) {
        // Scan through table looking for exited children.
        havekids = 0;

        defer_enter(&proc_df);
        acquire(&proc_lst_lock);

        for (iter = proc_lst_head.next; iter != &proc_lst_head; iter = iter->next) {
            struct proc *pp = (struct proc *)iter;
            acquire(&pp->lock);

            if (pp->parent == p) {
                // make sure the child isn't still in exit() or swtch().
                havekids = 1;
                if (pp->state == ZOMBIE) {
                    // Found one.

                    // remove proc struct from list
                    lst_remove(&proc_lst_head, &pp->proc_lst_node);
                    release(&proc_lst_lock);

                    pid = pp->pid;
                    int copy_ok = 1;

                    if (addr != 0 &&
                        copyout(p->pagetable, addr, (char *)&pp->xstate,
                                sizeof(pp->xstate)) < 0) {
                        copy_ok = 0;
                    }

                    freeproc(pp);
                    // put proc struct into defer queue; will be freed
                    int deferred = defer_ptr(&proc_df, pp);
                    release(&pp->lock);

                    // If we cannot defer freeing (e.g. out of memory),
                    // free the proc struct right away to avoid leaks.
                    if (deferred < 0)
                        kfree(pp);

                    release(&wait_lock);
                    defer_exit(&proc_df);

                    defer_reclaim(&proc_df);

                    return copy_ok == 1 ? pid : -1;
                }

            }
            release(&pp->lock);

        }
        release(&proc_lst_lock);
        defer_exit(&proc_df);

        // No point waiting if we don't have any children.
        if (!havekids || killed(p)) {
            release(&wait_lock);
            return -1;
        }

        // Wait for a child to exit.
        sleep(p, &wait_lock); // DOC: wait-sleep
    }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void) {
    struct proc *p;
    struct cpu *c = mycpu();

    c->proc = 0;
    for (;;) {
        // The most recent process to run may have had interrupts
        // turned off; enable them to avoid a deadlock if all
        // processes are waiting.
        intr_on();

        // Free proc structs that nobody requires
        defer_reclaim(&proc_df);

        int found = 0;

        struct list *iter;
        defer_enter(&proc_df);
        acquire(&proc_lst_lock);

        for (iter = proc_lst_head.next; iter != &proc_lst_head; iter = iter->next) {
            p = (struct proc *)iter;

            acquire(&p->lock);
            release(&proc_lst_lock);
            if (p->state == RUNNABLE) {
                // Switch to chosen process.  It is the process's job
                // to release its lock and then reacquire it
                // before jumping back to us.

                p->state = RUNNING;
                c->proc = p;

                defer_exit(&proc_df);
                swtch(&c->context, &p->context);
                defer_enter(&proc_df);

                // Process is done running for now.
                // It should have changed its p->state before coming back.
                c->proc = 0;
                found = 1;
            }
            release(&p->lock);

            acquire(&proc_lst_lock);
        }
        release(&proc_lst_lock);
        defer_exit(&proc_df);

        if (found == 0) {
            // nothing to run; stop running on this core until an interrupt.
            intr_on();
            asm volatile("wfi");
        }
    }
}
```

= Заключение

В ходе выполнения лабораторной работы был внедрён `buddy allocator` в xv6 и выполнен переход от статического хранения объектов к динамическому выделению памяти.

В первой части лабораторной работы структуры файлов были переведены со статического массива на выделение через аллокатор, что снимает жёсткое ограничение по количеству одновременно открытых файлов и уменьшает неоправданный расход памяти ядра.

Во второй части была применена оптимизация метаданных `buddy allocator`: вместо хранения отдельного бита занятости для каждого блока используется xor-бит на пару соседних блоков. Это позволяет освободить дополнительный объём оперативной памяти.

В третьей части `buddy allocator` был применён для структур процессов. Для безопасного освобождения объектов при конкурентных обходах использован механизм отложенного освобождения (`defer reclaim`), предотвращающий `use-after-free`(`UAF`).


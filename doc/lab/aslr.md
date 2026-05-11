# ASLR (Address Space Layout Randomization)

## Концепция

Изначально, в `xv6` отсутствует `ASLR`. Проверить это можно, запустив следующую программу сначала в `xv6`, а затем в `Linux` (ниже версия программы для `xv6`, для `Linux` будут другие include'ы):

```c
#include "kernel/types.h"
#include "user/user.h"

int global;

int main(void) {
    int local = 0;
    void *heap = malloc(16);

    printf("code main:   %p\n", main);
    printf("data global: %p\n", &global);
    printf("stack local: %p\n", &local);
    printf("heap malloc: %p\n", heap);

    free(heap);
    exit(0);
}
```

Результат запуска программы в `xv6`:
```
$ aslrcheck
exec aslrcheck
code main:   0x0000000000000000
data global: 0x0000000000001000
stack local: 0x0000000000003FAC
heap malloc: 0x0000000000013FF0
$ aslrcheck
exec aslrcheck
code main:   0x0000000000000000
data global: 0x0000000000001000
stack local: 0x0000000000003FAC
heap malloc: 0x0000000000013FF0
$ aslrcheck
exec aslrcheck
code main:   0x0000000000000000
data global: 0x0000000000001000
stack local: 0x0000000000003FAC
heap malloc: 0x0000000000013FF0
```

Как видно из вывода, виртуальные адреса сегментов после перезапуска не меняются.

Результат запуска программы в `Linux (Ubuntu)`:
```
user@user:~/aslr-test$ ./a.out 
code main:   0x5b0c7b2de1a9
data global: 0x5b0c7b2e1014
stack local: 0x7ffe2ec9f73c
heap malloc: 0x5b0ca53d92a0
user@user:~/aslr-test$ ./a.out 
code main:   0x6414b4ec01a9
data global: 0x6414b4ec3014
stack local: 0x7ffe3f3ea6fc
heap malloc: 0x6414f00382a0
user@user:~/aslr-test$ ./a.out 
code main:   0x628270ffd1a9
data global: 0x628271000014
stack local: 0x7ffecd6e887c
heap malloc: 0x6282a84092a0
user@user:~/aslr-test$ ./a.out 
code main:   0x62dbefdf21a9
data global: 0x62dbefdf5014
stack local: 0x7ffef635919c
heap malloc: 0x62dc257332a0
```

Как видно из вывода, виртуальные адреса сегментов после каждого перезапуска меняются. В `Linux` за эту политику применения отвечает переменная `randomize_va_space`. Вывести её содержимое можно следующим образом:

```sh
cat /proc/sys/kernel/randomize_va_space
2
```

Значение `2` означает полное применение `ASLR`. Помимо `2`, есть ещё 2 режима:
- `0`: `ASLR` отключён
- `1`: `ASLR` применяется частично

Попробуем отключить `ASLR`:

```sh
sudo tee /proc/sys/kernel/randomize_va_space
0
```

Совершим несколько запусков повторно и посмотрим на виртуальные адреса секторов:

```
user@user:~/aslr-test$ ./a.out 
code main:   0x5555555551a9
data global: 0x555555558014
stack local: 0x7fffffffe28c
heap malloc: 0x5555555592a0
user@user:~/aslr-test$ ./a.out 
code main:   0x5555555551a9
data global: 0x555555558014
stack local: 0x7fffffffe28c
heap malloc: 0x5555555592a0
user@user:~/aslr-test$ ./a.out 
code main:   0x5555555551a9
data global: 0x555555558014
stack local: 0x7fffffffe28c
heap malloc: 0x5555555592a0
user@user:~/aslr-test$ ./a.out 
code main:   0x5555555551a9
data global: 0x555555558014
stack local: 0x7fffffffe28c
heap malloc: 0x5555555592a0
```

Действительно, адреса не изменяются. Попробуем реализовать поведение, аналогичное `Linux`: будет 2 состояния для `ASLR` - рандомизация включена и выключена. 

Для начала надо понять, как сгенерировать случайное значение

## Получение сида

### Теория

`RNG` (`Random Number Generator`) делятся на 2 типа:
1. `True`
2. `Pseudo`

Сгенерировать "настоящее" случайное значение на `xv6` при эмуляции в `qemu` -- сложная задача, поэтому я решил брать сид ("настоящее" с.з.) с хоста (в моём случае хост -- `linux`), а уже его дальше прогонять через функции, генерирующие псевдо-случайное значение.

В `linux` случайное значение, вычисленное из энтропии (движения мыши, нажатия клавиш и т.д.) можно получить из файлов `/dev/urandom` или `/dev/random`. `u` значит в данном случае неблокирующий. В большинстве случаев используются значения именно из `urandom`. 

### Подключение `virtio-rng` через `QEMU`

Здесь выполнял подключение, схожее с описанным в [статье о сетевом драйвере](https://habr.com/ru/articles/826500/).

Для обмена с хостом используется специальное [`virtio`-устройство `rng`](https://wiki.qemu.org/Features/VirtIORNG). Подключим аналогично блочному устройство через `virtio MMIO`:

```Makefile
...
# connecting blk device -- reference
QEMUOPTS += -drive file=fs.img,if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
...
# adding random number generator (RNG) from host as a seed
QEMUOPTS += -object rng-random,filename=/dev/urandom,id=rng0
QEMUOPTS += -device virtio-rng-device,rng=rng0,bus=virtio-mmio-bus.1
```

`bus=virtio-mmio-bus.1`: здесь 1, так как каждое значение должно находиться в отдельном "слоте" на шине => регистры адаптера будут отображены в память.

`qemu` эмулирует архитектуру `risc-v`, в которой ВУ (внешние устройства) описываются через `device-tree` (`DT`). Каждое ВУ описывается веткой (нодой) в `DT`.

Цитата из статьи:
> Код определяет адреса регистров устройства по смещению базового адреса. Пример: базовый адрес virtio-диска - VIRTIO0 = 0x10001000. Найдем базовый адрес сетевого адаптера. QEMU печатает дерево устройств, когда получит опцию dumpdtb. Программа dtc преобразует дерево из двоичного формата в текст.

Сделаем дамп, указав в `Makefile` следующий параметр:
```Makefile
QEMUOPTS += -machine dumpdtb=virt.dtb
```

Преобразуем блоб в текстовое описание дерева:
```sh
dtc -I dtb -O dts -o virt.dts virt.dtb
```

Нужные ноды:
```dts
virtio_mmio@10002000 {
    interrupts = <0x02>;
    interrupt-parent = <0x07>;
    reg = <0x00 0x10002000 0x00 0x1000>;
    compatible = "virtio,mmio";
};

virtio_mmio@10001000 {
    interrupts = <0x01>;
    interrupt-parent = <0x07>;
    reg = <0x00 0x10001000 0x00 0x1000>;
    compatible = "virtio,mmio";
};
```

### Код взаимодействия с хостом

```memlayout.h
// Physical memory layout

// qemu -machine virt is set up like this,
// based on qemu's hw/riscv/virt.c:
//
// 00001000 -- boot ROM, provided by qemu
// 02000000 -- CLINT
// 0C000000 -- PLIC
// 10000000 -- uart0
// 10001000 -- virtio disk
// 10002000 -- virtio rng
// 80000000 -- boot ROM jumps here in machine mode
//             -kernel loads the kernel here
// unused RAM after 80000000.

// the kernel uses physical memory thus:
// 80000000 -- entry.S, then kernel text and data
// end -- start of kernel page allocation area
// PHYSTOP -- end RAM used by the kernel

// qemu puts UART registers here in physical memory.
#define UART0 0x10000000L
#define UART0_IRQ 10

// virtio mmio interface
#define VIRTIO0 0x10001000
#define VIRTIO0_IRQ 1

#define VIRTIO1 0x10002000
```

Не будем обрабатывать прерывания, так как сид будем получать лишь единожды при старте системы.

> Драйвер общается с устройством, когда пишет и читает регистры устройства. Добавим область памяти с регистрами адаптера в таблицу страниц ядра - иначе драйвер получит ошибку доступа к памяти, когда обратится к регистру адаптера
```c
// virtio mmio rng interface
kvmmap(kpgtbl, VIRTIO1, VIRTIO1, PGSIZE, PTE_R | PTE_W);
```

Дальше жесть убийства читать продолжение [тут...](/kernel/virt/virtio_rng.c). При написании кода опирался в основном на уже существующий в `xv6` [код драйвера блочного устройства](/kernel/virt/virtio_disk.c) и на [код сетевого драйвера из статьи](https://github.com/sa2304/xv6-riscv/blob/15e51e66874eaff922041de0ab89671b066b2a16/kernel/virtio_net.c).

Самая важная для написания драйвера информация в следующем разделе!

#### Очереди VirtIO

> Драйвер и устройство передают друг другу буферы памяти с помощью очередей. Поместить буфер в VirtIO-очередь сложнее, чем добавить элемент к односвязному списку. Пристегнитесь.
>
> Очередь VirtIO содержит три массива:
>   - Массив дескрипторов. Каждый дескриптор описывает буфер памяти.
>   - Массив available содержит номера дескрипторов, которые драйвер передал устройству.
>   - Массив used содержит номера дескрипторов, которые устройство передало драйверу.
>
> Драйвер ищет свободный дескриптор, заполняет буфер и пишет номер дескриптора в массив available, чтобы передать буфер устройству. Устройство пишет номер дескриптора в массив used, когда возвращает буфер драйверу.
> Устройство не пишет в массив дескрипторов и работает только с буферами, которые предоставил драйвер. Драйвер заполняет очередь входящих пакетов буферами, чтобы адаптер принимал пакеты из сети.

## Устройство ELF-файла

Для понимания таблиц релокация необходима краткая справка касательно устройства `ELF`-файла. Она описана в [этом файле](./elf/elf.md).

## Рандомизация стека

Сдвинуть стек возможно даже и без `PIE`-бинарных файлов, это относительно несложно сделать: достаточно аллоцировать дополнительно случайное число страниц

`exec.c`:

```c
...
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
...
```

## Рандомизация `code`, `rodata`, `data`, `heap`

Для рандомизации адресов данных секций уже недостаточно просто сдвинуть адрес начала секции

Обычные `EXEC`-бинарники уже содержат фиксированные виртуальные адреса, поэтому сначала пользовательские программы необходимо перевести в формат `PIE` (`Position Independent Executable`)

В `Makefile` для user-программ добавлены флаги:

```Makefile
CFLAGS += -fPIE
LDUSERFLAGS = -pie -e start
```

`-fPIE` заставляет компилятор генерировать `position-independent` код (`PIC`). `-pie` заставляет линкер собрать итоговый `ELF` как `DYN`. Флаг `-e start` нужен, чтобы `entry point` `ELF` указывал на функцию `start` из `user/ulib.c`: именно она вызывает `main()`, а затем `exit()`. Без этого исполняемый файл мог бы начинать выполнение не с `xv6`-старта

Старый linker script `user/user.ld` для PIE не используется, потому что он задавал фиксированную раскладку с адреса `0`. Вместо него применяется стандартный linker script `riscv64-linux-gnu-ld` (он применяется по умолчанию при линковке)

### Случайная база загрузки

В `exec()` теперь выбирается случайная база загрузки:

```c
uint64 load_base = randomize_va_space ? get_random(1, 257) * PGSIZE : 0;
```

Для каждого `LOAD`-сегмента реальный виртуальный адрес считается так:

```c
real_va = load_base + ph.vaddr;
```

`Entry point` также сдвигается на эту базу:

```c
p->trapframe->epc = load_base + elf.entry;
```

Именно поэтому адреса `main`, глобальных переменных и строк начинают отличаться при разных запусках

### Невыровненные LOAD-сегменты

`PIE` исполняемые файлы могут иметь `LOAD`-сегменты, у которых `ph.vaddr` не выровнен по странице, например:

```sh
LOAD Offset 0x1ee8 VirtAddr 0x2ee8
```

Старая версия `exec()` требовала выравнивания виртуального адреса загрузки в память по размеру страницы:

```c
ph.vaddr % PGSIZE == 0
```

Для `PIE` это неверно. Так как операции маппинга и аллокации, реализованные в `xv6` требуют выровнивания виртуального адреса по странице, то все виртуальные адреса `LOAD`-секций были округлены до адреса страниц. С помощью изменения сдвигов в ходе чтения файла все необходимые данные будут загружены в память, а обращение по виртуальным адресам будет успешно разрешено:

```c
uint64 pageoff = ph.vaddr - PGROUNDDOWN(ph.vaddr);
uint64 va0 = load_base + PGROUNDDOWN(ph.vaddr);
uint64 offset0 = ph.off - pageoff;
uint64 filesz0 = ph.filesz + pageoff;
```

### Релокации R_RISCV_RELATIVE

Некоторые PIE-бинарники содержат секцию `.rela.dyn` с релокациями типа `R_RISCV_RELATIVE`. Такая релокация означает: по адресу `load_base + offset` нужно записать значение `load_base + addend`

В `exec()` после загрузки всех `LOAD`-сегментов читаются section headers, находятся секции типа `SHT_RELA`, а затем обрабатываются записи:

```c
uint64 value = load_base + relocation.addend;
copyout(pagetable, load_base + relocation.offset, (char *)&value, sizeof(value));
```

Это нужно, например, для глобальных указателей вида:

```c
char *argv[] = {"sh", 0};
```

Без обработки релокации указатель продолжал бы указывать на старый адрес, рассчитанный для базы `0`

### Проверка

После изменений `aslrcheck` показывает, что меняются адреса кода, данных, строк, стека и heap:

```text
$ aslrcheck
code main:   0x000000000002E27A
data global: 0x0000000000031000
stack local: 0x000000000006AFAC
heap malloc: 0x000000000007AFF0
ro data:     0x000000000002EC38
$ aslrcheck
code main:   0x000000000001F27A
data global: 0x0000000000022000
stack local: 0x0000000000117FAC
heap malloc: 0x0000000000127FF0
ro data:     0x000000000001FC38
```

Смещения внутри PIE при этом сохраняются. Например, `main` по-прежнему находится по смещению `0x27a` от базы загрузки, но сама база каждый раз выбирается случайно

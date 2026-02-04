# ASLR (Address Space Layout Randomization)

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

Как видно из вывода, виртуальные адреса сегментов после каждого перезапуска меняются. В `Linux` за это политику применения отвечает переменная `randomize_va_space`. Вывести её содержимое можно следующим образом:

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

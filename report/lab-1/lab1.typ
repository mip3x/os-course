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

#let work_title = "Лабораторная работа №1"
#let work_name = "Введение в Xv6"

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

*Ссылка на Pull Request:*
#link("https://github.com/mip3x/os-course/pull/1")

В ходе лабораторной работы были выполнены следующие шаги:

1. Собрана и запущена через `qemu` ОС `xv6`
2. Реализована пользовательская программа `user/pingpong.c`:
   - создан pipe;
   - выполнен `fork()` для создания дочернего процесса;
   - организован обмен сообщениями "ping" / "pong" через pipe;
   - выведены сообщения в формате `<pid>: got <message>`;
   - добавлены проверки ошибок системных вызовов и освобождение ресурсов
3. Добавлена сборка программы `pingpong` через `UPROGS` в `Makefile`
4. Реализован системный вызов `dump`:
   - добавлено объявление в `user/user.h`;
   - добавлена генерация user-space обёртки через `user/usys.pl`;
   - добавлена реализация в `kernel/proc.c` с чтением значений регистров `s2—s11` из `trapframe` текущего процесса;
   - организована печать младших 32 бит каждого регистра
5. Подключён системный вызов `dump` в syscall-цепочку:
   - добавлен номер в `kernel/syscall.h`;
   - добавлена функция-обёртка в `kernel/sysproc.c`;
   - добавлено сопоставление номера и обработчика в `kernel/syscall.c`;
   - добавлена сигнатура в `kernel/defs.h`;
6. Выполнено тестирование вызова `dump` `user/dumptests.c`
7. Реализован системный вызов `dump2` с проверками прав доступа, валидацией аргументов и возвратом значений в user-space через `copyout`, после чего выполнено тестирование `user/dump2tests.c`

= Промежуточные результаты

== Часть 1. Pingpong

Реализована программа `user/pingpong.c`, обменивающаяся сообщениями между родительским и дочерним процессами через `pipe`

Логика работы:
- родитель пишет строку `"ping"` в pipe;
- дочерний процесс читает сообщение, печатает `child<pid ...>: got ping` и отправляет `"pong"` обратно;
- родитель читает ответ и печатает `parent<pid ...>: got pong`

=== Код

*Листинг `user/pingpong.c`*

```c
#include "kernel/types.h"
#include "kernel/file/stat.h"
#include "user/user.h"

#define BUFFER_LEN 5
#define EOF '\0'

#define EXIT_PIPE_ERROR 1
#define EXIT_FORK_ERROR 2
#define EXIT_READ_ERROR 3
#define EXIT_EOF_REACHED_ERROR 4
#define EXIT_WRITE_ERROR 5
#define EXIT_WAIT_ERROR 6

void close_pipe(int pipefds[2]) {
    if (close(pipefds[0]) == -1) {
        fprintf(2, "failed to close pipefds[0]!\n");
    }

    if (close(pipefds[1]) == -1) {
        fprintf(2, "failed to close pipefds[1]!\n");
    }
}

int main() {
    int errno = 0;
    int pipefds[2];
    if (pipe(pipefds) == -1) {
        fprintf(2, "pipe syscall error!\n");
        errno = EXIT_PIPE_ERROR;
        exit(errno);
    }

    int pid = fork();
    if (pid == -1) {
        fprintf(2, "fork syscall error!\n");
        errno = EXIT_FORK_ERROR;
        goto cleanup;
    }

    if (pid > 0) {
        #define PARENT_MSG "ping"
        if (write_retry(pipefds[1], PARENT_MSG, strlen(PARENT_MSG)) == -1) {
            fprintf(2, "parent: write syscall error!\n");
            errno = EXIT_WRITE_ERROR;
            goto cleanup;
        }

        pid = wait((int *)0);
        if (pid == -1) {
            fprintf(2, "wait syscall error!\n");
            errno = EXIT_WAIT_ERROR;
            goto cleanup;
        }

        char buffer[BUFFER_LEN];
        int bytes_to_read = BUFFER_LEN - 1;
        int bytes_read = read_retry(pipefds[0], buffer, bytes_to_read);

        if (bytes_read == -1) {
            fprintf(2, "parent: read syscall error!\n");
            errno = EXIT_READ_ERROR;
            goto cleanup;
        }
        if (bytes_read < bytes_to_read) {
            fprintf(2, "parent: EOF reached!\n");
            errno = EXIT_EOF_REACHED_ERROR;
            goto cleanup;
        }

        buffer[bytes_read] = EOF;

        int parent_pid = getpid();
        printf("parent<pid %d>: got %s\n", parent_pid, buffer);
    } else {
        char buffer[BUFFER_LEN];
        int bytes_to_read = BUFFER_LEN - 1;
        int bytes_read = read_retry(pipefds[0], buffer, bytes_to_read);

        if (bytes_read == -1) {
            fprintf(2, "child: read syscall error!\n");
            errno = EXIT_READ_ERROR;
            goto cleanup;
        }
        if (bytes_read < bytes_to_read) {
            fprintf(2, "child: EOF reached!\n");
            errno = EXIT_EOF_REACHED_ERROR;
            goto cleanup;
        }

        buffer[bytes_read] = EOF;

        printf("child<pid %d>: got %s\n", pid, buffer);

        #define CHILD_MSG "pong"
        if (write_retry(pipefds[1], CHILD_MSG, strlen(CHILD_MSG)) == -1) {
            fprintf(2, "child: write syscall error!\n");
            errno = EXIT_WRITE_ERROR;
            goto cleanup;
        }
    }

cleanup:
    close_pipe(pipefds);
    exit(errno);
}
```

== Часть 2. Dump

Добавлен системный вызов `dump`, печатающий значения регистров `s2—s11` текущего процесса

Реализация размещена в `kernel/proc.c`. Значения регистров извлекаются из `trapframe`. Доступ к полям `s2—s11` организован через указатель на `p->trapframe->s2` и последовательный проход по диапазону регистров

=== Код

*Листинг `kernel/proc.c`*

```c
// Print values of registers s2-s11
void dump(void) {
    struct proc *p = myproc();

    uint64 *s11_offset = &((struct trapframe *)0)->s11;
    uint64 *s2_offset = &((struct trapframe *)0)->s2;
    uint8 registers_n = (uint8)(s11_offset - s2_offset + 1);

    uint64 *registers_offset = &(p->trapframe->s2);
    for (uint8 i = 0; i < registers_n; i++) {
        uint64 register_value_64 = *(registers_offset + i);
        uint32 register_value_32 = (uint32)register_value_64;

        printf("s%d = %d\n", i + 2, register_value_32);
    }
}
```

*Листинг `kernel/sysproc.c`*

```c
uint64 sys_dump(void) {
    dump();
    return 0;
}
```

== Часть 3. Dump2

Реализован системный вызов `dump2(pid, register_num, return_value)`, возвращающий значение одного регистра `s2—s11` для процесса `pid` в user-space память по адресу `return_value`

Особенности реализации:
- добавлена валидация номера регистра (`register_num` должен быть в диапазоне 2..11), иначе возвращается `-3`;
- реализована проверка прав: смотреть регистры процесса может только сам процесс и его предки; при отсутствии прав возвращается `-1`;
- при отсутствии процесса с указанным `pid` возвращается `-2`;
- передача результата в user-space выполняется через `copyout`, так как прямой доступ к пользовательскому адресу из ядра невозможен; при ошибке записи возвращается `-4`;
- при успехе системный вызов возвращает `0`

=== Код

*Листинг `kernel/proc.c`*

```c
// Print register s#register_num in current state of process #pid
int dump2(int pid, int register_num, uint64 *return_value) {
    if (register_num < 2 || register_num > 11) {
        return -3;
    }

    struct proc *cur_proc = myproc();
    struct proc *target_proc;

    acquire(&cur_proc->lock);
    if (pid == cur_proc->pid) {
        target_proc = cur_proc;
        release(&cur_proc->lock);
        acquire(&target_proc->lock);
        goto return_register_value;
    }
    release(&cur_proc->lock);

    for (target_proc = proc; target_proc < &proc[NPROC]; target_proc++) {
        acquire(&target_proc->lock);
        if (pid == target_proc->pid) {
            acquire(&wait_lock);
            struct proc *parent = target_proc->parent;

            while (parent) {
                if (parent == cur_proc) {
                    release(&wait_lock);
                    goto return_register_value;
                }

                parent = parent->parent;
            }

            release(&wait_lock);
            release(&target_proc->lock);
            return -1;
        }
        release(&target_proc->lock);
    }

    return -2;

return_register_value:
    uint64 *registers_offset = &(target_proc->trapframe->s2);

    // we need to get offset from `s2` field in struct `trapframe`
    // that's why we need to reduce `register_num` by 2 to get index
    // register_num = 2 => offset from s2 = 0
    uint8 register_index = register_num - 2;
    uint64 register_value = *(registers_offset + register_index);

    release(&target_proc->lock);

    if (copyout(cur_proc->pagetable,
                *return_value,
                (char*)&register_value,
                sizeof(uint64)) == -1) {
        return -4;
    }

    return 0;
}
```

*Листинг `kernel/sysproc.c`*

```c
uint64 sys_dump2(void) { 
    int pid, register_num;
    uint64 return_value;

    argint(0, &pid);
    argint(1, &register_num);
    argaddr(2, &return_value);

    return dump2(pid, register_num, &return_value);
}
```

= Заключение

В ходе выполнения лабораторной работы были изучены основные механизмы взаимодействия процессов в xv6, а также принципы реализации системных вызовов.

Были реализованы пользовательская программа для обмена данными между процессами через `pipe` и системные вызовы для получения значений регистров процессов.

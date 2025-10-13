// init: The initial user-level program

#include "kernel/types.h"
#include "kernel/file/stat.h"
#include "kernel/locking/spinlock.h"
#include "kernel/locking/sleeplock.h"
#include "kernel/file/fs.h"
#include "kernel/file/file.h"
#include "user/user.h"
#include "kernel/file/fcntl.h"

char *argv[] = {"sh", 0};

int main(void) {
    int pid, wpid;

    if (open("console", O_RDWR) < 0) {
        mknod("console", CONSOLE, 0);
        open("console", O_RDWR);
    }
    dup(0); // stdout
    dup(0); // stderr

    for (;;) {
        printf("init: starting sh\n");
        pid = fork();
        if (pid < 0) {
            printf("init: fork failed\n");
            exit(1);
        }
        if (pid == 0) {
            exec("sh", argv);
            printf("init: exec sh failed\n");
            exit(1);
        }

        for (;;) {
            // this call to wait() returns if the shell exits,
            // or if a parentless process exits.
            wpid = wait((int *)0);
            if (wpid == pid) {
                // the shell exited; restart it.
                break;
            } else if (wpid < 0) {
                printf("init: wait returned an error\n");
                exit(1);
            } else {
                // it was a parentless process; do nothing.
            }
        }
    }
}

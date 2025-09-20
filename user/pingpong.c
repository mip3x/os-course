#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define BUFFER_LEN 5
#define EOL '\0'

int main() {
    int p[2];
    if (pipe(p) == -1) {
        fprintf(2, "pipe syscall error!\n");
        exit(1);
    }

    int pid = fork();
    if (pid > 0) {
        #define PARENT_MSG "ping"
        write(p[1], PARENT_MSG, strlen(PARENT_MSG));

        pid = wait((int *)0);

        char buffer[BUFFER_LEN];
        int bytes_read = read(p[0], buffer, BUFFER_LEN - 1);
        buffer[bytes_read] = EOL;

        if (bytes_read == 0) {
            fprintf(2, "parent got E0F while reading pipe!\n");
            exit(2);
        }

        int parent_pid = getpid();
        printf("parent<pid %d>: got %s\n", parent_pid, buffer);
    } else {
        char buffer[BUFFER_LEN];
        int bytes_read = read(p[0], buffer, BUFFER_LEN - 1);
        buffer[bytes_read] = EOL;

        if (bytes_read == 0) {
            fprintf(2, "child got E0F while reading pipe!\n");
            exit(2);
        }

        printf("child<pid %d>: got %s\n", pid, buffer);

        #define CHILD_MSG "pong"
        write(p[1], CHILD_MSG, strlen(CHILD_MSG));

        exit(0);
    }
}
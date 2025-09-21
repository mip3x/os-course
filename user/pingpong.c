#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define BUFFER_LEN 5
#define EOL '\0'

#define EXIT_PIPE_ERROR 1
#define EXIT_FORK_ERROR 2
#define EXIT_READ_ERROR 3
#define EXIT_WRITE_ERROR 4
#define EXIT_WAIT_ERROR 5

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
        if (write(pipefds[1], PARENT_MSG, strlen(PARENT_MSG)) == -1) {
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
        int bytes_read = read(pipefds[0], buffer, bytes_to_read);

        if (bytes_read < bytes_to_read) {
            fprintf(2, "parent: read syscall error!\n");
            errno = EXIT_READ_ERROR;
            goto cleanup;
        }
        buffer[bytes_read] = EOL;

        int parent_pid = getpid();
        printf("parent<pid %d>: got %s\n", parent_pid, buffer);
    } else {
        char buffer[BUFFER_LEN];
        int bytes_to_read = BUFFER_LEN - 1;
        int bytes_read = read(pipefds[0], buffer, bytes_to_read);

        if (bytes_read < bytes_to_read) {
            fprintf(2, "child: read syscall error!\n");
            errno = EXIT_READ_ERROR;
            goto cleanup;
        }
        buffer[bytes_read] = EOL;

        printf("child<pid %d>: got %s\n", pid, buffer);

        #define CHILD_MSG "pong"
        if (write(pipefds[1], CHILD_MSG, strlen(CHILD_MSG)) == -1) {
            fprintf(2, "child: write syscall error!\n");
            errno = EXIT_WRITE_ERROR;
            goto cleanup;
        }
    }

cleanup:
    close_pipe(pipefds);
    exit(errno);
}
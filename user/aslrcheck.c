#include "kernel/types.h"
#include "user/user.h"

int global;

int main(void) {
    int local = 0;
    void *heap = malloc(16);
    char *hello_string = "hello";

    printf("code main:   %p\n", main);
    printf("data global: %p\n", &global);
    printf("stack local: %p\n", &local);
    printf("heap malloc: %p\n", heap);
    printf("ro data:     %p\n", hello_string);

    free(heap);
    exit(0);
}

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

// target.c
#include <stdio.h>
#include <unistd.h>

int main() {
    int a = 0;
    printf("PID: %d\n", getpid());
    while (1) {
        printf("&a = %p, a = %d\n", (void*)&a, a);
        sleep(1);
    }
    return 0;
}

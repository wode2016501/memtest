// modifier.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <pid> <address_hex>\n", argv[0]);
        return 1;
    }

    pid_t pid = atoi(argv[1]);
    unsigned long addr = strtoul(argv[2], NULL, 16);

    char mem_path[64];
    snprintf(mem_path, sizeof(mem_path), "/proc/%d/mem", pid);

    int fd = open(mem_path, O_RDWR);
    if (fd == -1) {
        perror("open /proc/pid/mem");
        return 1;
    }

    // 移动到目标地址
    if (lseek(fd, (off_t)addr, SEEK_SET) == -1) {
        perror("lseek");
        close(fd);
        return 1;
    }

    int new_value = 99;   // 要写入的新值
    ssize_t n = write(fd, &new_value, sizeof(new_value));
    if (n == sizeof(new_value)) {
        printf("Successfully wrote %d to address %p\n", new_value, (void*)addr);
    } else {
        perror("write");
    }

    close(fd);
    return 0;
}


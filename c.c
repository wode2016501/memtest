// scanner.c - 扫描目标进程内存，将值为 99 的 int 改为 100
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>

#define CHUNK_SIZE (1024 * 1024)   // 每次读取 1MB

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "用法: %s <目标进程PID>\n", argv[0]);
        return 1;
    }

    pid_t pid = atoi(argv[1]);
    char maps_path[64];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    char mem_path[64];
    snprintf(mem_path, sizeof(mem_path), "/proc/%d/mem", pid);

    FILE *maps = fopen(maps_path, "r");
    if (!maps) {
        perror("打开 maps 失败");
        return 1;
    }

    int mem_fd = open(mem_path, O_RDWR);
    if (mem_fd == -1) {
        perror("打开 mem 失败");
        fclose(maps);
        return 1;
    }

    char line[1024];
    int target = 99;
    int newval = 100;
    int found = 0;

    while (fgets(line, sizeof(line), maps)) {
        unsigned long start, end;
        char perms[5] = {0};
        // 解析地址范围及权限
        if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) != 3)
            continue;
        // 只关心可写区域 (权限中包含 'w')
        if (!strchr(perms, 'w'))
            continue;

        printf("扫描区域: 0x%lx-0x%lx (%s)\n", start, end, perms);
        unsigned long addr = start;
        while (addr < end) {
            size_t chunk = (end - addr) < CHUNK_SIZE ? (end - addr) : CHUNK_SIZE;
            unsigned char *buffer = malloc(chunk);
            if (!buffer) {
                perror("malloc 失败");
                break;
            }
            if (lseek(mem_fd, addr, SEEK_SET) == -1) {
                free(buffer);
                break;
            }
            ssize_t bytes = read(mem_fd, buffer, chunk);
            if (bytes <= 0) {
                free(buffer);
                break;
            }
            // 在缓冲区中搜索 int 类型的目标值
            for (size_t offset = 0; offset <= (size_t)bytes - sizeof(int); offset++) {
                int *p = (int*)(buffer + offset);
                if (*p == target) {
                    unsigned long hit_addr = addr + offset;
                    printf("找到目标值 %d 于地址 0x%lx\n", target, hit_addr);
                    // 修改该地址的值
                    /*if (lseek(mem_fd, hit_addr, SEEK_SET) != -1 &&
                        write(mem_fd, &newval, sizeof(newval)) == sizeof(newval)) {
                        printf("成功修改为 %d\n", newval);
                        found = 1;
                        free(buffer);
                        goto cleanup;
                    } else {
                        perror("写入失败");
                    }
		    */
                }
            }
            addr += bytes;
            free(buffer);
        }
    }

cleanup:
    close(mem_fd);
    fclose(maps);
    if (!found)
        printf("未找到目标值 %d\n", target);
    return 0;
}

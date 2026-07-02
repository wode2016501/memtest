// scanner.c - 扫描目标进程内存，将值为 99 的 int 改为 100
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <arpa/inet.h>  // 包含 inet_addr 的声明
#include <netinet/in.h> // 包含相关结构体定义

#define CHUNK_SIZE (1024 * 1024) // 每次读取 1MB

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        fprintf(stderr, "用法: %s <目标进程PID> <地址> <新值>\n", argv[0]);
        return 1;
    }
    pid_t pid = atoi(argv[1]); // 目标进程的PID
    unsigned long addr= strtoul(argv[2], NULL, 0); // 要扫描的内存地址 // 0x736c219174
    int new_value = atoi(argv[3]); // 新的值
    if(!pid || !addr|| !new_value)
    {
        fprintf(stderr, "无效的参数 pid=%d, addr=%lx, new_value=%d\n", pid, addr, new_value);
        return 1;
    }
    int ip = inet_addr("192.168.100.254");
    int newip = inet_addr("8.8.8.8");

    char mem_path[64];
    snprintf(mem_path, sizeof(mem_path), "/proc/%d/mem", pid);

    int mem_fd = open(mem_path, O_RDWR);
    if (mem_fd == -1)
    {
        perror("打开 mem 失败");
        return 1;
    }

    
    if (lseek(mem_fd, addr, SEEK_SET) != -1 &&
        write(mem_fd, &newip, sizeof(newip)) == sizeof(newip))
    {
        printf("成功修改 %d 的地址 0x%lx 为 %d\n", pid, addr, newip);
    }
    else
    {
        perror("写入失败");
    }

cleanup:
    close(mem_fd);
    return 0;
}

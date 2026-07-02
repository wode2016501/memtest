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
#include <signal.h>
#define CHUNK_SIZE (1024 * 1024) // 每次读取 1MB
#define MAX_CHUNKS 611000        // 最多读取 n 个块
int running = 1;
void handle_signal(int sig)
{
    running = 0;
}
int getdata(int fd, long addr, void *buf, size_t size)
{
    if (lseek(fd, addr, SEEK_SET) == -1)
    {
        perror("lseek 失败");
        return -1;
    }
    ssize_t bytes = read(fd, buf, size);
    if (bytes != size)
    {
        perror("read 失败");
        return -1;
    }
    return 0;
}
int setdata(int fd, long addr, void *buf, size_t size)
{
    if (lseek(fd, addr, SEEK_SET) == -1)
    {
        perror("lseek 失败");
        return -1;
    }
    ssize_t bytes = write(fd, buf, size);
    if (bytes != size)
    {
        perror("write 失败");
        return -1;
    }
    return 0;
}
int main(int argc, char *argv[])
{
    signal(SIGINT, handle_signal);
    if (argc != 3)
    {
        fprintf(stderr, "用法: %s <目标进程PID> <数值>\n", argv[0]);
        return 1;
    }
    int ip = inet_addr("192.168.100.254");
    int newip = inet_addr("8.8.8.8");
    pid_t pid = atoi(argv[1]);
    int target = atoi(argv[2]);
    ip = target;
    char maps_path[64];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    char mem_path[64];
    snprintf(mem_path, sizeof(mem_path), "/proc/%d/mem", pid);

    FILE *maps = fopen(maps_path, "r");
    if (!maps)
    {
        perror("打开 maps 失败");
        return 1;
    }


    int    mem_fd = open(mem_path, O_RDWR);
    if (mem_fd == -1)
    {
        perror("打开 mem 失败");
        fclose(maps);
        return 1;
    }

    char line[1024];
    unsigned long *longaddr = malloc(MAX_CHUNKS * sizeof(unsigned long));
    unsigned long *intaddr = malloc(MAX_CHUNKS * sizeof(unsigned long));
    unsigned long *floataddr = malloc(MAX_CHUNKS * sizeof(unsigned long));
    unsigned long *doubleaddr = malloc(MAX_CHUNKS * sizeof(unsigned long));
    int doublecount = 0;
    int floatcount = 0;
    int intcount = 0;
    int longcount = 0;
    char *buffer = malloc(CHUNK_SIZE);
    if (!buffer)
    {
        perror("malloc 失败");
        goto cleanup;
    }
    while (running && fgets(line, sizeof(line), maps))
    {
        unsigned long start, end;
        char perms[5] = {0};
        // 解析地址范围及权限
        if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) != 3)
            continue;
        // 只关心可写区域 (权限中包含 'w')
        if (!strchr(perms, 'w'))
            continue;

        // printf("8扫描区域: 0x%lx-0x%lx (%s)  %ld\n", start, end, perms, end - start);
        printf("扫描区域: %s", line);
        unsigned long addr = start;

        while (addr < end - sizeof(long) && running)
        {
            size_t chunk = (end - addr) < CHUNK_SIZE ? (end - addr) : CHUNK_SIZE;

            if (lseek(mem_fd, addr, SEEK_SET) == -1)
            {
                free(buffer);
                break;
            }
            ssize_t bytes = read(mem_fd, buffer, chunk);
            if (bytes <= 0)
            {
                free(buffer);
                break;
            }

            // 在缓冲区中搜索 int 类型的目标值
            for (size_t offset = 0; offset <= (size_t)bytes - sizeof(int); offset++)
            {
                int *p = (int *)(buffer + offset);
                float *p1 = (float *)(buffer + offset);
                long *p3 = (long *)(buffer + offset);
                double *p2 = (double *)(buffer + offset);

                if (*p == ip /*target*/)
                {
                    unsigned long hit_addr = addr + offset;
                    // printf("扫描区域: 0x%lx-0x%lx (%s)  %ld\n", start, end, perms, end - start);
                    // printf("int 找到目标值 %d 于地址 0x%lx\n", ip, hit_addr);
                    if (intcount >= MAX_CHUNKS)
                    {
                        printf("intaddr 数组已满，无法存储更多地址。\n");
                        continue;
                    }
                    intaddr[intcount] = hit_addr;
                    intcount++;
                }
                if (*p1 == ip)
                {
                    unsigned long hit_addr = addr + offset;
                    // printf("扫描区域: 0x%lx-0x%lx (%s)  %ld\n", start, end, perms, end - start);
                    // printf("float 找到目标值 %d 于地址 0x%lx\n", ip, hit_addr);
                    if (floatcount >= MAX_CHUNKS)
                    {
                        printf("floataddr 数组已满，无法存储更多地址。\n");
                        continue;
                    }
                    floataddr[floatcount] = hit_addr;
                    floatcount++;
                }
                if (offset <= (size_t)bytes - sizeof(long))
                {
                    if (*p2 == ip)
                    {
                        unsigned long hit_addr = addr + offset;
                        // printf("扫描区域: 0x%lx-0x%lx (%s)  %ld\n", start, end, perms, end - start);
                        // printf("double 找到目标值 %d 于地址 0x%lx\n", ip, hit_addr);
                        if (doublecount >= MAX_CHUNKS)
                        {
                            printf("doubleaddr 数组已满，无法存储更多地址。\n");
                            continue;
                        }
                        doubleaddr[doublecount] = hit_addr;
                        doublecount++;
                    }
                    if (*p3 == ip)
                    {
                        unsigned long hit_addr = addr + offset;
                        // printf("扫描区域: 0x%lx-0x%lx (%s)  %ld\n", start, end, perms, end - start);
                        // printf("long 找到目标值 %d 于地址 0x%lx\n", ip, hit_addr);
                        if (longcount >= MAX_CHUNKS)
                        {
                            printf("longaddr 数组已满，无法存储更多地址。\n");
                            continue;
                        }
                        longaddr[longcount] = hit_addr;
                        longcount++;
                    }
                }
            }

            addr += chunk - sizeof(long); // 确保不会错过跨块的值
        }
    }
    free(buffer);
    printf("=========================扫描完成==============================\n");
    running = 1;
    int new_value = 0; // 新的值
    int ivalue = 0;
    float fvalue = 0;
    double dvalue = 0;
    long lvalue = 0;
    int ret = 0;
    int count = 0;
    int n = 0;
    char w = 0;
    while (running)
    {
        new_value = 0; // 重置新值
        n = 0;
        memset(line, 0, 20); // 清空输入缓冲区
        ret = read(0, line, 100);
        if (ret <= 0)
            break;
        if (running == 0)
        {
            break;
        }
        w = *line;
        if (w == 'n')
        {
            printf("请输入搜索值 (整数):\n");
            ret = read(0, &line, 50);
            if (ret <= 0)
                break;
            line[ret] = '\0'; // 确保字符串以 null 结尾
            new_value = atoi(line);
            if (new_value > 0)
                ip = new_value;
            else
            {
                printf("无效的新值，请重新输入。\n");
                continue;
            }
        }
        new_value = 0;
        if (w == 'w' || w == 'q')
        {
            printf("请输入要修改的行号和新值 (格式: 行号)，或按 Ctrl+D 退出:\n");
            ret = read(0, &line, sizeof(line));
            if (ret <= 0)
                break;
            line[ret] = '\0'; // 确保字符串以 null 结尾
            n = atoi(line);
            if (n > intcount + floatcount + doublecount + longcount)
            {
                printf("行号超出范围，请重新输入。\n");
                continue;
            }
            if (n > 0 || w == 'q')
            {
                printf("请输入新值 (整数):\n");
                ret = read(0, &line, sizeof(line));
                if (ret <= 0)
                    break;
                line[ret] = '\0'; // 确保字符串以 null 结尾
                new_value = atoi(line);
            }
        }
        count = 0;
        for (int i = 0; i < intcount; i++)
        {
            count++;
            getdata(mem_fd, intaddr[i], &ivalue, sizeof(ivalue));
            printf("line:%d int 原值: %d 0x%lx \n", count, ivalue, intaddr[i]);
            if (w == 'n' && ip != ivalue)
            {
                memcpy(intaddr + i, intaddr + i + 1, (intcount - i - 1) * sizeof(unsigned long));
                intcount--;
                printf("line:%d int 原值: %d != %d\n", count, ivalue, ip);
                count--;
                i--;
                continue;
            }
            if ((n == count || w == 'q') && new_value > 0)
            {
                setdata(mem_fd, intaddr[i], &new_value, sizeof(new_value));
            }
        }
        for (int i = 0; i < floatcount; i++)
        {
            count++;
            getdata(mem_fd, floataddr[i], &fvalue, sizeof(fvalue));
            printf("line:%d float 原值: %f 0x%lx\n", count, fvalue, floataddr[i]);
            if (w == 'n' && ip != fvalue)
            {
                memcpy(floataddr + i, floataddr + i + 1, (floatcount - i - 1) * sizeof(unsigned long));
                floatcount--;
                printf("line:%d float 原值: %f != %d, 跳过修改\n", count, fvalue, ip);
                count--;
                i--;
                continue;
            }
            if ((n == count || w == 'q') && new_value > 0)
            {
                fvalue = (float)new_value;
                setdata(mem_fd, floataddr[i], &fvalue, sizeof(fvalue));
            }
        }
        for (int i = 0; i < doublecount; i++)
        {
            count++;
            getdata(mem_fd, doubleaddr[i], &dvalue, sizeof(dvalue));
            printf("line:%d double 原值: %lf 0x%lx\n", count, dvalue, doubleaddr[i]);
            if (w == 'n' && ip != dvalue)
            {
                memcpy(doubleaddr + i, doubleaddr + i + 1, (doublecount - i - 1) * sizeof(unsigned long));
                doublecount--;
                printf("line:%d double 原值: %lf != %d, 跳过修改\n", count, dvalue, ip);
                count--;
                i--;
                continue;
            }
            if ((n == count || w == 'q') && new_value > 0)
            {
                dvalue = (double)new_value;
                setdata(mem_fd, doubleaddr[i], &dvalue, sizeof(dvalue));
            }
        }
        for (int i = 0; i < longcount; i++)
        {
            count++;
            getdata(mem_fd, longaddr[i], &lvalue, sizeof(lvalue));
            printf("line:%d long 原值: %ld 0x%lx\n", count, lvalue, longaddr[i]);
            if (w == 'n' && ip != lvalue)
            {
                memcpy(longaddr + i, longaddr + i + 1, (longcount - i - 1) * sizeof(unsigned long));
                longcount--;
                printf("line:%d long 原值: %ld != %d, 跳过修改\n", count, lvalue, ip);
                count--;
                i--;
                continue;
            }
            if ((n == count || w == 'q') && new_value > 0)
            {
                lvalue = (long)new_value;
                setdata(mem_fd, longaddr[i], &lvalue, sizeof(lvalue));
            }
        }
    }

cleanup:
    free(intaddr);
    free(floataddr);
    free(doubleaddr);
    free(longaddr);
    close(mem_fd);
    fclose(maps);
    return 0;
}

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
#define CHUNK_SIZE (2 * 1024 * 1024) // 每次读取 1MB
#define MAX_CHUNKS 611000            // 最多读取 n 个块
int running = 1;
void handle_signal(int sig)
{
    running = 0;
}
int read1_agemap_entry(int fd, unsigned long vaddr, size_t page_size, char *buffer, long vendaddr)
{
    off_t offset = (vaddr / page_size) * sizeof(uint64_t);
    int size = (vendaddr / page_size) * sizeof(uint64_t) - offset + sizeof(uint64_t);
    // 使用 pread 原子性地读取，避免更改文件偏移
    ssize_t n = pread(fd, buffer, size, offset);
    if (n != size)
    {
        if (n < 0)
            perror("pread");
        else
            fprintf(stderr, "Read %zd bytes, expected %zu at offset %ld\n", n, size, offset);
        exit(EXIT_FAILURE);
    }
    return size;
}
// 读取指定虚拟地址对应的 pagemap 条目（64位）
static uint64_t read_pagemap_entry(int fd, unsigned long vaddr, size_t page_size)
{
    off_t offset = (vaddr / page_size) * sizeof(uint64_t);
    uint64_t entry;
    // 使用 pread 原子性地读取，避免更改文件偏移
    ssize_t n = pread(fd, &entry, sizeof(entry), offset);
    if (n != sizeof(entry))
    {
        if (n < 0)
            perror("pread");
        else
            fprintf(stderr, "Read %zd bytes, expected %zu at offset %ld\n", n, sizeof(entry), offset);
        exit(EXIT_FAILURE);
    }
    return entry;
}

// 从 pagemap 条目中提取物理页框号（PFN）
static uint64_t extract_pfn(uint64_t entry)
{
    // 第63位：页面是否在内存中
    if (!(entry & (1ULL << 63)))
    {
        //  fprintf(stderr, "Page not present in memory (swapped or unmapped)\n");
        return 0;
    }
    // PFN 位于 bits 0~54
    return entry & ((1ULL << 55) - 1);
}
unsigned long virt_to_phys(int fd, unsigned long vaddr, size_t page_size, long pagemap_start, char *pagemap_buffer, long pchunk)
{
    off_t offset = (vaddr / page_size) * sizeof(uint64_t);
    if (offset > pchunk)
    {
        fprintf(stderr, "Offset %ld is out of bounds for pagemap buffer size %ld\n", offset, pchunk);
        return 0;
    }
    uint64_t entry = *(uint64_t *)(pagemap_buffer + (offset - pagemap_start));
    uint64_t pfn = extract_pfn(entry);
    if (!pfn)
        return 0;
    unsigned long page_offset = vaddr % page_size;
    return (pfn * page_size) + page_offset;
}
// 获取系统页面大小（通常为 4096）
static size_t get_page_size(void)
{
    long sz = sysconf(_SC_PAGESIZE);
    if (sz < 0)
    {
        perror("sysconf");
        exit(EXIT_FAILURE);
    }
    return (size_t)sz;
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
    int count = 0;
    int ip = inet_addr("192.168.100.254");
    int newip = inet_addr("8.8.8.8");
    pid_t pid = atoi(argv[1]);
    int target = atoi(argv[2]);
    ip = target;
    char maps_path[64];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    char mem_path[64];
    snprintf(mem_path, sizeof(mem_path), "/proc/%d/mem", pid);
    char pagemap_path[256];
    snprintf(pagemap_path, sizeof(pagemap_path), "/proc/%d/pagemap", pid);
    int page_fd = open(pagemap_path, O_RDONLY);
    if (page_fd < 0)
    {
        perror("打开 pagemap 失败");
        return 1;
    }
    FILE *maps = fopen(maps_path, "r");
    if (!maps)
    {
        perror("打开 maps 失败");
        return 1;
    }

    int mem_fd = open("/dev/mem", O_RDWR);
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
    int size = 8 * 1024 * 1024 * 1024;
    char *buffer = malloc(10000); /* = mmap(NULL, size,
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED,  // 注意：修改会同步回物理内存
                          mem_fd, 0);*/
    if (!buffer)
    {
        perror("malloc 失败");
        goto cleanup;
    }
    int page_size = get_page_size();
    char *pagemap_buffer = 0;
    int ret = 0;
    while (running && fgets(line, sizeof(line), maps))
    {
        unsigned long start, end;
        long pagemap_start, pagemap_end;
        char perms[5] = {0};
        // 解析地址范围及权限
        if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) != 3)
            continue;
        // 只关心可写区域 (权限中包含 'w')
        if (!strchr(perms, 'w'))
            continue;
        if (strchr(line, '/'))// 过滤掉文件映射区域，只保留匿名映射
            continue;

        // printf("8扫描区域: 0x%lx-0x%lx (%s)  %ld\n", start, end, perms, end - start);
        printf("--------------------------size: %ld 扫描区域: %s", end - start, line);
        pagemap_start = (start / page_size) * sizeof(uint64_t);
        pagemap_end = (end / page_size) * sizeof(uint64_t);
        // long pagemap_size = pagemap_end - pagemap_start + sizeof(uint64_t);

        unsigned long addr = 0;
        while (start < end - sizeof(long) && running)
        {
            int chunk = (end - start) < CHUNK_SIZE ? (end - start) : CHUNK_SIZE;
            long pchunk = ((start + chunk) / page_size) * sizeof(uint64_t);
            // printf("pagemap_start: %ld pagemap_end: %ld pchunk: %ld  %ld\n", pagemap_start, pagemap_end, pchunk, chunk);

            pagemap_buffer = malloc(pchunk - pagemap_start);
            if (pagemap_buffer < 0)
            {
                printf("malloc size: %d", pchunk - pagemap_start);
                perror("malloc 失败0");
                goto cleanup;
            }
            ret = read1_agemap_entry(page_fd, start, page_size, pagemap_buffer, start + chunk);
            if (ret < 0)
            {
                fprintf(stderr, "读取 pagemap 失败: 读取了 %d 字节，预期 %d 字节\n", ret, pagemap_end - pagemap_start + sizeof(uint64_t));
                free(pagemap_buffer);
                goto cleanup;
            }
            for (int i = 0; i < chunk; i++)
            {
                if (running == 0)
                    break;

                unsigned long phy_addr = virt_to_phys(page_fd, start + i, page_size, pagemap_start, pagemap_buffer, pchunk);
                if (phy_addr == 0)
                {
                    continue; // 页面不在内存中，跳过
                }
                long phy_end_addr = 0;
                int y = 0;
                int ypage_size = page_size;
                if (chunk - i < page_size)
                    ypage_size = chunk - i;
                for (int j = 0; j < ypage_size; j++)
                {
                    if (running == 0)
                        break;
                    y = ypage_size - j;
                    if (y > 0)
                    {
                        phy_end_addr = virt_to_phys(page_fd, start + i + y, page_size, pagemap_start, pagemap_buffer, pchunk);
                        if (phy_end_addr != 0 && phy_end_addr == phy_addr + y)
                        {
                            break;
                        }
                    }
                    phy_end_addr = 0;
                }
                int phy_size = 1;
                if (phy_end_addr != 0)
                {
                    phy_size = phy_end_addr - phy_addr;
                }
                addr = phy_addr;
                // 在缓冲区中搜索 int 类型的目标值

                ret = pread(mem_fd, buffer, phy_size, phy_addr);
                if (ret != phy_size)
                {
                    perror("pread 失败");
                    break;
                }

                for (size_t offset = 0; offset <= phy_size - sizeof(int); offset++)
                {
                    int *p = (int *)(buffer + offset);
                    float *p1 = (float *)(buffer + offset);
                    long *p3 = (long *)(buffer + offset);
                    double *p2 = (double *)(buffer + offset);
                    if (*p == ip)
                    {
                        //  printf("int 找到目标值 %d 于地址 0x%lx\n", ip, p);
                        unsigned long hit_addr = addr + offset;

                        if (intcount >= MAX_CHUNKS)
                        {
                            printf("intaddr 数组已满，无法存储更多地址。\n");
                            continue;
                        }
                        intaddr[intcount] = hit_addr;
                        intcount++;
                        count++;
                    }

                    if (*p1 == ip)
                    {
                        unsigned long hit_addr = addr + offset;
                        if (floatcount >= MAX_CHUNKS)
                        {
                            printf("floataddr 数组已满，无法存储更多地址。\n");
                            continue;
                        }
                        floataddr[floatcount] = hit_addr;
                        floatcount++;
                        count++;
                    }
                    if (offset <= (size_t)phy_size - sizeof(long))
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
                            count++;
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
                            count++;
                        }
                    }
                }
                i += phy_size; // 跳过已扫描的物理页
            }
            start += chunk - sizeof(long); // 确保不会错过跨块的值
            free(pagemap_buffer);
            pagemap_buffer = NULL;
        }
    }
    if (pagemap_buffer != 0)
        free(pagemap_buffer);
    free(buffer);
    printf("=========================扫描完成%d个==============================\n", count);
    running = 1;
    int new_value = 0; // 新的值
    int ivalue = 0;
    float fvalue = 0;
    double dvalue = 0;
    long lvalue = 0;
    int n = 0;
    char w = 0;
    while (running && count)
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
            if (w == 'w')
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

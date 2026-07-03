#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#define CHUNK_PAGES 6400
int PAGE_SIZE = 4096;
#define MAX_CHUNKS 611000
unsigned long *longaddr = 0;
unsigned long *vlongaddr = 0;
unsigned long *intaddr = 0;
unsigned long *vintaddr = 0;
unsigned long *floataddr = 0;
unsigned long *vfloataddr = 0;
unsigned long *doubleaddr = 0;
unsigned long *vdoubleaddr = 0;
int doublecount = 0;
int floatcount = 0;
int intcount = 0;
int longcount = 0;
int count = 0;
int mem_fd =0;
int running = 1;



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

int intint(char *map_ptr, int map_size,  unsigned long phys_start, unsigned long current_vaddr_start, unsigned  long val)
{
    int value = (int )val;
    if(map_size>PAGE_SIZE)printf("扫描大小：%d 值：%d 物理地址： 0x%lx 虚拟地址： 0x%lx\n",map_size,value,phys_start,current_vaddr_start);
    for (size_t offset = 0; offset <= map_size - sizeof(int); offset++)
    {
        //printf("扫描大小：%d 值：%d 物理地址： 0x%lx 虚拟地址： 0x%lx\n",map_size,value,phys_start+offset,current_vaddr_start+offset);
        int *p = (int *)(map_ptr + offset);
        float *p1 = (float *)(map_ptr + offset);
        long *p3 = (long *)(map_ptr + offset);
        double *p2 = (double *)(map_ptr + offset);
        if (*p == value)
        {
            //printf("int 找到目标值 %d 于地址 0x%lx\n", value, p);
            unsigned long hit_addr = phys_start + offset;

            if (intcount >= MAX_CHUNKS)
            {
                printf("intaddr 数组已满，无法存储更多地址。\n");
                continue;
            }
            intaddr[intcount] = hit_addr;
            vintaddr[intcount] = current_vaddr_start + offset;
            intcount++;
            count++;
        }

        if (*p1 == value)
        {
            unsigned long hit_addr = phys_start+offset;
            if (floatcount >= MAX_CHUNKS)
            {
                printf("floataddr 数组已满，无法存储更多地址。\n");
                continue;
            }
            floataddr[floatcount] = hit_addr;
            vfloataddr[floatcount] = current_vaddr_start + offset;
            floatcount++;
            count++;
        }
        if (offset <= (size_t)map_size - sizeof(long))
        {
            if (*p2 == value)
            {
                unsigned long hit_addr = phys_start + offset;
                // printf("扫描区域: 0x%lx-0x%lx (%s)  %ld\n", start, end, perms, end - start);
                // printf("double 找到目标值 %d 于地址 0x%lx\n", ip, hit_addr);
                if (doublecount >= MAX_CHUNKS)
                {
                    printf("doubleaddr 数组已满，无法存储更多地址。\n");
                    continue;
                }
                doubleaddr[doublecount] = hit_addr;
                vdoubleaddr[doublecount] = current_vaddr_start + offset;
                doublecount++;
                count++;
            }
            if (*p3 == value)
            {
                unsigned long hit_addr = phys_start + offset;
                // printf("扫描区域: 0x%lx-0x%lx (%s)  %ld\n", start, end, perms, end - start);
                // printf("long 找到目标值 %d 于地址 0x%lx\n", ip, hit_addr);
                if (longcount >= MAX_CHUNKS)
                {
                    printf("longaddr 数组已满，无法存储更多地址。\n");
                    continue;
                }
                longaddr[longcount] = hit_addr;
                vlongaddr[longcount] = current_vaddr_start + offset;
                longcount++;
                count++;
            }
        }
    }
}


void *print_addr(long value){
     printf("=========================扫描完成%d个==============================\n", count);
        running = 1;
        int ip=(int )value;
    int new_value = 0; // 新的值
    char line[4096];
    int ivalue = 0;
    float fvalue = 0;
    double dvalue = 0;
    long lvalue = 0;
    int n = 0;
    char w = 0;
    int ret=0;
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
            printf("line:%d int 原值: %d 物理地址: 0x%lx 虚拟地址: 0x%lx\n", count, ivalue, intaddr[i], vintaddr[i]);
            if (w == 'n' && ip != ivalue)
            {
                memcpy(intaddr + i, intaddr + i + 1, (intcount - i - 1) * sizeof(unsigned long));
                memcpy(vintaddr + i, vintaddr + i + 1, (intcount - i - 1) * sizeof(unsigned long));
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
            printf("line:%d float 原值: %f 物理地址: 0x%lx 虚拟地址: 0x%lx\n", count, fvalue, floataddr[i], vfloataddr[i]);
            if (w == 'n' && ip != fvalue)
            {
                memcpy(floataddr + i, floataddr + i + 1, (floatcount - i - 1) * sizeof(unsigned long));
                memcpy(vfloataddr + i, vfloataddr + i + 1, (floatcount - i - 1) * sizeof(unsigned long));
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
            printf("line:%d double 原值: %lf 物理地址: 0x%lx 虚拟地址: 0x%lx\n", count, dvalue, doubleaddr[i], vdoubleaddr[i]);
            if (w == 'n' && ip != dvalue)
            {
                memcpy(doubleaddr + i, doubleaddr + i + 1, (doublecount - i - 1) * sizeof(unsigned long));
                memcpy(vdoubleaddr + i, vdoubleaddr + i + 1, (doublecount - i - 1) * sizeof(unsigned long));
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
            printf("line:%d long 原值: %ld 物理地址: 0x%lx 虚拟地址: 0x%lx\n", count, lvalue, longaddr[i], vlongaddr[i]);
            if (w == 'n' && ip != lvalue)
            {
                memcpy(longaddr + i, longaddr + i + 1, (longcount - i - 1) * sizeof(unsigned long));
                memcpy(vlongaddr + i, vlongaddr + i + 1, (longcount - i - 1) * sizeof(unsigned long));
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
}


int chunked_fast_read_physical_memory(int mem_fd, int pagemap_fd, uint64_t raw_vaddr, size_t read_size, long value)
{
    int result = -1;
    // 1. 拆分虚拟地址：向下对齐页边界，提取页内余数
    uint64_t aligned_vaddr = raw_vaddr & ~(PAGE_SIZE - 1); // 整除
    uint64_t page_offset = raw_vaddr & (PAGE_SIZE - 1);    // 余数

    // 计算这次任务总共跨越了多少个页
    size_t total_pages = (page_offset + read_size + PAGE_SIZE - 1) / PAGE_SIZE; // 总页数

    // 2. 打开两个关键的底层文件
    // snprintf(path, sizeof(path), "/proc/%d/pagemap", pid);
    // pagemap_fd = open(path, O_RDONLY);
    // mem_fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (pagemap_fd < 0 || mem_fd < 0)
    {
        perror("[-] 打开 pagemap 或 /dev/mem 失败（需要 root 权限）");
        return result;
    }

    // 3. 准备一个 64 个页大小的【局部账单数组】（只需固定占用 512 字节内存，极省资源）
    uint64_t pfn_chunk[CHUNK_PAGES];
    size_t pages_processed = 0; // 当前已经处理了多少个页
    size_t bytes_copied = 0;    // 当前已经拷贝了多少字节数据

    // ================= 外层循环：每次拉取最多 64 个页的映射账单 =================
    while (pages_processed < total_pages)
    {
        // 计算当前这一轮需要读多少个页的账单（最后一轮可能不足 64 页）
        size_t current_chunk_pages = total_pages - pages_processed;
        if (current_chunk_pages > CHUNK_PAGES)
        {
            current_chunk_pages = CHUNK_PAGES;
        }

        // 计算在 pagemap 文件中对应的绝对字节偏移量
        uint64_t current_vaddr_start = aligned_vaddr + (pages_processed * PAGE_SIZE);
        off_t seek_offset = (current_vaddr_start / PAGE_SIZE) * 8;

        // 【系统调用】一刀切读取当前这 64 页的物理映射关系
        if (pread(pagemap_fd, pfn_chunk, current_chunk_pages * 8, seek_offset) != (ssize_t)(current_chunk_pages * 8))
        {
            perror("[-] 读取局部 pagemap 失败");
            return result;
        }

        // 解析当前这 64 页的物理基地址
        for (size_t i = 0; i < current_chunk_pages; i++)
        {
            uint64_t entry = pfn_chunk[i];
            if (entry & (1ULL << 63))
            {          
                    unsigned long page_offset = current_vaddr_start % PAGE_SIZE;
                   pfn_chunk[i] =((entry & ((1ULL << 55) - 1)) * PAGE_SIZE) + page_offset;                                                     // Present 位为 1
               // pfn_chunk[i] = (entry & 0x7FFFFFFFFFFFFFCEULL) * PAGE_SIZE; // 得到物理地址
            }
            else
            {
                pfn_chunk[i] = 0;
            }
        }

        //printf("虚拟地址： 0x%lx  物理地址： 0x%lx  值： %ld\n",current_vaddr_start, pfn_chunk[0], value);

        // ================= 内层循环：在当前 64 页内寻找物理连续块并 mmap =================
        size_t i = 0;
        while (i < current_chunk_pages)
        {
            if (pfn_chunk[i] == 0)
            {
                // 如果发现该页没有分配物理内存，跳过它
                i++;
                continue;
            }

            // 智能探测连续块：在当前的 64 页数组里，往后看有多少页物理上是连着的
            size_t contig_pages = 1;
            while (i + contig_pages < current_chunk_pages && pfn_chunk[i + contig_pages] == pfn_chunk[i] + (contig_pages * PAGE_SIZE))
            {
                contig_pages++;
            }

            size_t map_size = contig_pages * PAGE_SIZE;
            uint64_t phys_start = pfn_chunk[i];

            // 【大招】把这块局部连续的物理内存进行 mmap
            void *map_ptr = mmap(NULL, map_size, PROT_READ, MAP_SHARED, mem_fd, phys_start);
            if (map_ptr == MAP_FAILED)
            {
                perror("[-] 局部 mmap 失败");
                return result;
            }
             unsigned long vaddr= current_vaddr_start + i * PAGE_SIZE;
            intint(map_ptr, map_size, phys_start, vaddr, value);
            // 搬完立刻解映射
            munmap(map_ptr, map_size);

            i += contig_pages; // 跳过这个连续大块
        }

        pages_processed += current_chunk_pages; // 跳过这 64 个页，进入外层下一次循环
    }

    if (bytes_copied == read_size)
    {
        result = 0; // 成功
    }

    return result;
}

// =================== 测试用的 Main 函数 ===================
int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("使用方法: sudo  %s <目标PID>  数值\n", argv[0]);
        printf("示例: sudo %s 1234  100\n", argv[0]);
        return -1;
    }

    longaddr = malloc(MAX_CHUNKS * sizeof(unsigned long));
    if (longaddr <= 0)
    {
        perror("malloc failed");
        return -1;
    }
    vlongaddr = malloc(MAX_CHUNKS * sizeof(unsigned long));
    if (vlongaddr <= 0)
    {
        perror("malloc failed");
        return -1;
    }
    intaddr = malloc(MAX_CHUNKS * sizeof(unsigned long));
    if (intaddr <= 0)
    {
        perror("malloc failed");
        return -1;
    }
    vintaddr = malloc(MAX_CHUNKS * sizeof(unsigned long));
    if (vintaddr <= 0)
    {
        perror("malloc failed");
        return -1;
    }
    floataddr = malloc(MAX_CHUNKS * sizeof(unsigned long));
    if (floataddr <= 0)
    {
        perror("malloc failed");
        return -1;
    }
    vfloataddr = malloc(MAX_CHUNKS * sizeof(unsigned long));
    if (vfloataddr <= 0)
    {
        perror("malloc failed");
        return -1;
    }
    doubleaddr = malloc(MAX_CHUNKS * sizeof(unsigned long));
    if (doubleaddr <= 0)
    {
        perror("malloc failed");
        return -1;
    }
    vdoubleaddr = malloc(MAX_CHUNKS * sizeof(unsigned long));
    if (vdoubleaddr <= 0)
    {
        perror("malloc failed");
        return -1;
    }
    PAGE_SIZE = get_page_size();
    int target_pid = atoi(argv[1]);
    long value = strtoul(argv[2], NULL, 10);
   // uint64_t target_vaddr = 0; // strtoull(argv[2], NULL, 16);
    size_t read_len = 0;

    if (read_len % PAGE_SIZE != 0)
    {
        printf("读取长度必须是页大小的整数倍\n");
        return -1;
    }
    // 分配接收数据的缓冲区
    char *result_buf = (char *)malloc(read_len + 1);
    memset(result_buf, 0, read_len + 1);

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", target_pid);
    FILE *maps = fopen(path, "r");
    if (!maps)
    {
        perror("打开 maps 失败");
        return 1;
    }
    snprintf(path, sizeof(path), "/proc/%d/mem", target_pid);

    mem_fd = open("/dev/mem", O_RDWR);
    if (mem_fd == -1)
    {
        perror("打开 mem 失败");
        fclose(maps);
        return 1;
    }
    snprintf(path, sizeof(path), "/proc/%d/pagemap", target_pid);
    int page_fd = open(path, O_RDONLY);
    if (page_fd < 0)
    {
        perror("打开 pagemap 失败");
        return 1;
    }

    char line[4096];
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
        if (strchr(line, '/')) // 过滤掉文件映射区域，只保留匿名映射
            continue;
            read_len = end - start;

        // printf("8扫描区域: 0x%lx-0x%lx (%s)  %ld\n", start, end, perms, end - start);
        printf("--------------------------size: %ld 扫描区域: %s", end - start, line);
        chunked_fast_read_physical_memory(mem_fd, page_fd, start, read_len, value) ;
    }
    print_addr(value);
    //printf("[+] 开始极速读取 PID %d 的物理内存...\n", target_pid);
   /* if (chunked_fast_read_physical_memory(mem_fd, page_fd, target_vaddr, read_len, value) == 0)
    {

    }
    else
    {
        printf("[-] 读取失败。\n");
    }*/
    fclose(maps);
    close(mem_fd);
    close(page_fd);
    free(intaddr);
    free(vintaddr);
    free(floataddr);
    free(vfloataddr);
    free(doubleaddr);
    free(vdoubleaddr);
    free(longaddr);
    free(vlongaddr);
    free(result_buf);
    return 0;
}

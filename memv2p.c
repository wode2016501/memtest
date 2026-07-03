#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>


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
// 输入：进程PID，目标的起始虚拟地址，要读取的长度
// 输出：返回一个分配好的数组，里面存着每个页面的物理物理基地址
uint64_t *batch_translate_vpn_to_pfn(int pid, uint64_t start_vaddr, size_t size, size_t *page_count)
{
    char path[64];
    sprintf(path, "/proc/%d/pagemap", pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;

    *page_count = size / 4096;
    uint64_t *pfn_array = malloc((*page_count) * sizeof(uint64_t));

    // 计算在 pagemap 文件中的偏移量（虚拟页号 * 8 字节）
    off_t offset = (start_vaddr / 4096) * 8;
    lseek(fd, offset, SEEK_SET);

    // 关键高效点：一次性批量读取所有页面的映射数据（降低系统调用开销）
    if (read(fd, pfn_array, (*page_count) * 8) <= 0)
    {
        free(pfn_array);
        close(fd);
        return NULL;
    }
    close(fd);

    // 批量解析 PFN (Page Frame Number)
    for (size_t i = 0; i < *page_count; i++)
    {
        uint64_t entry = pfn_array[i];
        if (entry & (1ULL << 63))
        {                                                          // 检查 Present 位，确保该页当前确实在物理内存中
            pfn_array[i] = (entry & 0x7FFFFFFFFFFFFFCEULL) * 4096; // 提取并转换得到物理基地址
        }
        else
        {
            pfn_array[i] = 0; // 该页被交换到了Swap或尚未分配物理内存
        }
    }
    return pfn_array;
}

void *help()
{
    int mem_fd = open("/dev/mem", O_RDONLY | O_SYNC);
    char *buffer = malloc(size); // 接收物理内存数据的缓冲区

    size_t i = 0;
    while (i < page_count)
    {
        if (pfn_array[i] == 0)
        {
            i++;
            continue;
        }

        // 探测连续的物理内存块
        size_t chunk_pages = 1;
        while (i + chunk_pages < page_count &&
               pfn_array[i + chunk_pages] == pfn_array[i] + (chunk_pages * 4096))
        {
            chunk_pages++;
        }

        size_t chunk_size = chunk_pages * 4096;
        uint64_t phys_start = pfn_array[i];

        // 将这一整块连续的物理内存一次性映射过来
        void *map_ptr = mmap(NULL, chunk_size, PROT_READ, MAP_SHARED, mem_fd, phys_start);
        if (map_ptr != MAP_FAILED)
        {
            // 使用 CPU 硬件级的高速流式复制
            memcpy(buffer + (i * 4096), map_ptr, chunk_size);
            munmap(map_ptr, chunk_size);
        }

        i += chunk_pages; // 跳过这整个大块，继续找下一块
    }
}
int main() {
    int target_pid = 1234;                  // 目标进程 PID
    uint64_t vaddr = 0x7ffcedf00000;         // 从 maps 读到的起始虚拟地址
    size_t mem_size = 2 * 1024 * 1024;       // 2MB 大小

    // 1. 在外面定义一个变量，用来接收“页数”
    size_t total_pages = 0; 

    // 2. 传入 &total_pages（取地址）
    uint64_t *pfn_list = batch_translate_vpn_to_pfn(target_pid, vaddr, mem_size, &total_pages);

    if (pfn_list == NULL) {
        printf("翻译失败，请检查 root 权限或 PID 是否正确。\n");
        return -1;
    }

    // 3. 函数执行完后，total_pages 变量就已经自动被修改为了 512 (2MB / 4096)
    printf("成功翻译！该内存区域一共包含 %zu 个物理页：\n", total_pages);

    // 4. 使用这个 total_pages 来遍历结果
    for (size_t i = 0; i < total_pages; i++) {
        if (pfn_list[i] != 0) {
            printf("虚拟页 %zu -> 物理基地址: 0x%lx\n", i, pfn_list[i]);
        } else {
            printf("虚拟页 %zu -> 未分配物理内存(或在Swap中)\n", i);
        }
    }

    // 记得释放内存
    free(pfn_list);
    return 0;
}
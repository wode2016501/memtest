#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#define CHUNK_PAGES 6400
int PAGE_SIZE =4096;
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



int chunked_fast_read_physical_memory(int pid, uint64_t raw_vaddr, size_t read_size, char *out_buffer) {
    char path[64];
    int pagemap_fd = -1;
    int mem_fd = -1;
    int result = -1;

    // 1. 拆分虚拟地址：向下对齐页边界，提取页内余数
    uint64_t aligned_vaddr = raw_vaddr & ~(PAGE_SIZE - 1);//整除
    uint64_t page_offset = raw_vaddr & (PAGE_SIZE - 1); //余数

    // 计算这次任务总共跨越了多少个页
    size_t total_pages = (page_offset + read_size + PAGE_SIZE - 1) / PAGE_SIZE; //总页数

    // 2. 打开两个关键的底层文件
    snprintf(path, sizeof(path), "/proc/%d/pagemap", pid);
    pagemap_fd = open(path, O_RDONLY);
    mem_fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (pagemap_fd < 0 || mem_fd < 0) {
        perror("[-] 打开 pagemap 或 /dev/mem 失败（需要 root 权限）");
        goto cleanup;
    }

    // 3. 准备一个 64 个页大小的【局部账单数组】（只需固定占用 512 字节内存，极省资源）
    uint64_t pfn_chunk[CHUNK_PAGES];
    size_t pages_processed = 0; // 当前已经处理了多少个页
    size_t bytes_copied = 0;    // 当前已经拷贝了多少字节数据

    // ================= 外层循环：每次拉取最多 64 个页的映射账单 =================
    while (pages_processed < total_pages) {
        // 计算当前这一轮需要读多少个页的账单（最后一轮可能不足 64 页）
        size_t current_chunk_pages = total_pages - pages_processed;
        if (current_chunk_pages > CHUNK_PAGES) {
            current_chunk_pages = CHUNK_PAGES;
        }

        // 计算在 pagemap 文件中对应的绝对字节偏移量
        uint64_t current_vaddr_start = aligned_vaddr + (pages_processed * PAGE_SIZE);
        off_t seek_offset = (current_vaddr_start / PAGE_SIZE) * 8;

        // 【系统调用】一刀切读取当前这 64 页的物理映射关系
        if (pread(pagemap_fd, pfn_chunk, current_chunk_pages * 8, seek_offset) != (ssize_t)(current_chunk_pages * 8)) {
            perror("[-] 读取局部 pagemap 失败");
            goto cleanup;
        }

        // 解析当前这 64 页的物理基地址
        for (size_t i = 0; i < current_chunk_pages; i++) {
            uint64_t entry = pfn_chunk[i];
            if (entry & (1ULL << 63)) { // Present 位为 1
                pfn_chunk[i] = (entry & 0x7FFFFFFFFFFFFFCEULL) * PAGE_SIZE;//得到物理地址
            } else {
                pfn_chunk[i] = 0;
            }
        }

        // ================= 内层循环：在当前 64 页内寻找物理连续块并 mmap =================
        size_t i = 0;
        while (i < current_chunk_pages) {
            if (pfn_chunk[i] == 0) {
                // 如果发现该页没有分配物理内存，跳过它
                i++;
                continue;
            }

            // 智能探测连续块：在当前的 64 页数组里，往后看有多少页物理上是连着的
            size_t contig_pages = 1;
            while (i + contig_pages < current_chunk_pages && pfn_chunk[i + contig_pages] == pfn_chunk[i] + (contig_pages * PAGE_SIZE)) {
                contig_pages++;
            }

            size_t map_size = contig_pages * PAGE_SIZE;
            uint64_t phys_start = pfn_chunk[i];

            // 【大招】把这块局部连续的物理内存进行 mmap
            void *map_ptr = mmap(NULL, map_size, PROT_READ, MAP_SHARED, mem_fd, phys_start);
            if (map_ptr == MAP_FAILED) {
                perror("[-] 局部 mmap 失败");
                goto cleanup;
            }

            // 精准计算拷贝的起始余数（只有全盘读取的第一页才考虑原始偏移）
            size_t current_offset = (pages_processed == 0 && i == 0) ? page_offset : 0;  //余数
            size_t remaining_bytes = read_size - bytes_copied; // 剩余要拷贝多少字节
            size_t available_bytes = map_size - current_offset;
            size_t copy_len = (remaining_bytes < available_bytes) ? remaining_bytes : available_bytes;

            // 硬件级全速拷贝
            memcpy(out_buffer + bytes_copied, (char *)map_ptr + current_offset, copy_len);
            bytes_copied += copy_len;

            // 搬完立刻解映射
            munmap(map_ptr, map_size);

            i += contig_pages; // 跳过这个连续大块
        }

        pages_processed += current_chunk_pages; // 跳过这 64 个页，进入外层下一次循环
    }

    if (bytes_copied == read_size) {
        result = 0; // 成功
    }

cleanup:
    if (pagemap_fd >= 0) close(pagemap_fd);
    if (mem_fd >= 0) close(mem_fd);
    return result;
}


/**
 * 极速读取目标进程物理内存函数
 * @param pid           目标进程 PID
 * @param raw_vaddr     从 maps 拿到的任意虚拟地址（可带余数）
 * @param read_size     想要读取的数据总长度（字节）
 * @param out_buffer    存放读取结果的缓冲区（外部分配）
 * @return              成功返回 0，失败返回 -1
 */
int fast_read_physical_memory(int pid, uint64_t raw_vaddr, size_t read_size, char *out_buffer) {
    char path[64];
    int pagemap_fd = -1;
    int mem_fd = -1;
    uint64_t *pfn_array = NULL;
    int result = -1;

    // 1. 拆分虚拟地址：向下对齐页边界，并提取页内余数（Offset）
    uint64_t aligned_vaddr = raw_vaddr & ~(PAGE_SIZE - 1);
    uint64_t page_offset = raw_vaddr & (PAGE_SIZE - 1);

    // 2. 计算这次读取总共跨越了多少个物理页
    size_t total_pages = (page_offset + read_size + PAGE_SIZE - 1) / PAGE_SIZE;

    // 3. 打开 pagemap 文件并【一次性批量】读出所有页的物理地址清单
    snprintf(path, sizeof(path), "/proc/%d/pagemap", pid);
    pagemap_fd = open(path, O_RDONLY);
    if (pagemap_fd < 0) {
        perror("[-] 打开 pagemap 失败（通常需要 root 权限）");
        goto cleanup;
    }

    pfn_array = (uint64_t *)malloc(total_pages * sizeof(uint64_t));
    if (!pfn_array) goto cleanup;

    // 计算在 pagemap 文件中的绝对字节偏移量
    off_t seek_offset = (aligned_vaddr / PAGE_SIZE) * 8;
    // 使用 pread 代替 lseek+read，一行搞定，降低内核开销
    if (pread(pagemap_fd, pfn_array, total_pages * 8, seek_offset) != (ssize_t)(total_pages * 8)) {
        perror("[-] 批量读取 pagemap 账单失败");
        goto cleanup;
    }

    // 4. 在内存中极速批量解析 PFN，转换为真实的物理基地址
    for (size_t i = 0; i < total_pages; i++) {
        uint64_t entry = pfn_array[i];
        if (entry & (1ULL << 63)) { // Present 位为 1，说明在物理内存中
            pfn_array[i] = (entry & 0x7FFFFFFFFFFFFFCEULL) * PAGE_SIZE;
        } else {
            pfn_array[i] = 0; // 内存被交换到了 Swap 或根本未激活写入
        }
    }

    // 5. 打开 /dev/mem 准备进行硬件级高速映射
    mem_fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (mem_fd < 0) {
        perror("[-] 打开 /dev/mem 失败（需要 root 权限）");
        goto cleanup;
    }

    // 6. 核心优化：智能探测物理连续块，合并 mmap
    size_t i = 0;
    size_t bytes_copied = 0;

    while (i < total_pages) {
        if (pfn_array[i] == 0) {
            fprintf(stderr, "[-] 警告: 虚拟页 %zu 未分配物理内存(请确保目标进程已写入该内存)\n", i);
            i++;
            continue;
        }

        // 探测当前物理地址往后，有多少个页在物理上是死死连在一起的
        size_t chunk_pages = 1;
        while (i + chunk_pages < total_pages && 
               pfn_array[i + chunk_pages] == pfn_array[i] + (chunk_pages * PAGE_SIZE)) {
            chunk_pages++;
        }

        size_t chunk_size = chunk_pages * PAGE_SIZE;
        uint64_t phys_start = pfn_array[i];

        // 【大招】把这整块连续的物理内存，仅用 1 次 mmap 全部拉过来！
        void *map_ptr = mmap(NULL, chunk_size, PROT_READ, MAP_SHARED, mem_fd, phys_start);
        if (map_ptr == MAP_FAILED) {
            perror("[-] mmap 物理块失败");
            goto cleanup;
        }

        // 计算当前物理块在本次读取任务中的“首字节”和“要拷贝的长度”
        // 需要特殊处理第一块（有页内余数）和最后一块（可能不满一整页）
        size_t current_offset = (i == 0) ? page_offset : 0;
        size_t current_need_read = read_size - bytes_copied;
        size_t current_available = chunk_size - current_offset;
        size_t copy_len = (current_need_read < current_available) ? current_need_read : current_available;

        // 硬件级全速搬运
        memcpy(out_buffer + bytes_copied, (char *)map_ptr + current_offset, copy_len);
        bytes_copied += copy_len;

        // 搬完立刻释放这段虚拟映射窗口，防止撑爆本进程的虚拟空间
        munmap(map_ptr, chunk_size);

        i += chunk_pages; // 优雅地跳过整个连续大块
    }

    if (bytes_copied == read_size) {
        result = 0; // 100% 完美读取成功
    }

cleanup:
    if (pagemap_fd >= 0) close(pagemap_fd);
    if (mem_fd >= 0) close(mem_fd);
    if (pfn_array) free(pfn_array);
    return result;
}

// =================== 测试用的 Main 函数 ===================
int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("使用方法: sudo %s <目标PID> <虚拟地址(十六进制)> <读取长度(字节)>\n", argv[0]);
        printf("示例: sudo %s 1234 0x7ffcedf00520 100\n", argv[0]);
        return -1;
    }
    PAGE_SIZE = get_page_size();
    int target_pid = atoi(argv[1]);
    uint64_t target_vaddr = strtoull(argv[2], NULL, 16);
    size_t read_len = strtoul(argv[3], NULL, 10);
    if(read_len%PAGE_SIZE!=0){
        printf("读取长度必须是页大小的整数倍\n");
        return -1;
    }
    // 分配接收数据的缓冲区
    char *result_buf = (char *)malloc(read_len + 1);
    memset(result_buf, 0, read_len + 1);

    printf("[+] 开始极速读取 PID %d 的物理内存...\n", target_pid);
    if (fast_read_physical_memory(target_pid, target_vaddr, read_len, result_buf) == 0) {
        printf("[+] 读取成功！前 64 字节十六进制及 ASCII 数据打印如下：\n\n");
        
        // 漂亮的 Hex Dump 打印出来看看结果
        for (size_t i = 0; i < read_len && i < 64; i++) {
            printf("%02X ", (unsigned char)result_buf[i]);
            if ((i + 1) % 16 == 0 || i == read_len - 1 || i == 63) {
                printf("\n");
            }
        }
    } else {
        printf("[-] 读取失败。\n");
    }

    free(result_buf);
    return 0;
}

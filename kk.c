#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#define PAGE_SIZE 4096

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("使用方法: sudo %s <目标PID> <虚拟地址(十六进制)>\n", argv[0]);
        printf("示例: sudo %s 4567 0x7ffcedf00520\n", argv[0]);
        return -1;
    }

    int target_pid = atoi(argv[1]);
    uint64_t vaddr = strtoull(argv[2], NULL, 16);

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/pagemap", target_pid);

    int pagemap_fd = open(path, O_RDONLY);
    int mem_fd = open("/dev/mem", O_RDONLY | O_SYNC);

    if (pagemap_fd < 0 || mem_fd < 0) {
        perror("[-] 打开文件失败，请确保使用了 sudo 权限");
        if (pagemap_fd >= 0) close(pagemap_fd);
        if (mem_fd >= 0) close(mem_fd);
        return -1;
    }

    // 1. 精准计算当前地址在这一页内的余数（page_offset）
    unsigned long page_offset = vaddr % PAGE_SIZE;

    // 2. 定位并读取目标虚拟地址在 pagemap 文件中对应的 8 字节账单
    uint64_t entry = 0;
    off_t seek_offset = (vaddr / PAGE_SIZE) * 8;
    if (pread(pagemap_fd, &entry, 8, seek_offset) != 8) {
        perror("[-] 读取 pagemap 失败");
        close(pagemap_fd);
        close(mem_fd);
        return -1;
    }

    // 3. 检查 Present 位（第 63 位），确保页面在物理内存中
    if (!(entry & (1ULL << 63))) {
        fprintf(stderr, "[-] 错误: 目标地址目前不在物理内存中(请先在目标程序里输入数字激活它)\n");
        close(pagemap_fd);
        close(mem_fd);
        return -1;
    }

    // 4. 【核心：你的专属正确公式】
    // 使用标准的 55 位掩码抠出 PFN，膨胀 4096 倍后，立刻加上页内余数 page_offset
    // 算出一个带有尾巴的、直接指向变量的精确物理地址！
    uint64_t phys_addr = ((entry & ((1ULL << 55) - 1)) * PAGE_SIZE) + page_offset;
    printf("[+] 成功解析！目标虚拟地址对应的物理地址为: 0x%lx\n", phys_addr);
    uint64_t phys_addr1  = (entry & 0x7FFFFFFFFFFFFFCEULL) * PAGE_SIZE;
printf("[+] 成功解析！目标虚拟地址对应的物理地址为: 0x%lx\n", phys_addr1);
    // 5. 将这个带有尾巴的物理地址，直接丢给 mmap
    // 现代 Linux 内核会自动处理这个非 4096 对齐的物理地址，
    // 并且返回给我们的 map_ptr 指针会自动帮我们偏移好，直直地戳在数据对应的字节上！
    void *map_ptr = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, mem_fd, phys_addr);
    
    if (map_ptr == MAP_FAILED) {
        perror("[-] mmap 物理内存失败");
    } else {
        // 6. 因为内核已经帮我们做好了偏移，这里我们直接无脑读取一个 int 整数！
        int read_value = *(int *)map_ptr;
        printf("[+] 🎯 闪电读取成功！物理内存中的当前数值为: %d\n", read_value);

        munmap(map_ptr, PAGE_SIZE);
    }

    close(pagemap_fd);
    close(mem_fd);
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

// 获取系统页面大小（通常为 4096）
static size_t get_page_size(void) {
    long sz = sysconf(_SC_PAGESIZE);
    if (sz < 0) {
        perror("sysconf");
        exit(EXIT_FAILURE);
    }
    return (size_t)sz;
}

// 打开 /proc/<pid>/pagemap，返回文件描述符
static int open_pagemap(pid_t pid) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/pagemap", pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open pagemap");
        fprintf(stderr, "Hint: You need root or CAP_SYS_ADMIN to read pagemap.\n");
        exit(EXIT_FAILURE);
    }
    return fd;
}

// 读取指定虚拟地址对应的 pagemap 条目（64位）
static uint64_t read_pagemap_entry(int fd, unsigned long vaddr, size_t page_size) {
    off_t offset = (vaddr / page_size) * sizeof(uint64_t);
    uint64_t entry;
    // 使用 pread 原子性地读取，避免更改文件偏移
    ssize_t n = pread(fd, &entry, sizeof(entry), offset);
    if (n != sizeof(entry)) {
        if (n < 0) perror("pread");
        else fprintf(stderr, "Read %zd bytes, expected %zu at offset %ld\n", n, sizeof(entry), offset);
        exit(EXIT_FAILURE);
    }
    return entry;
}

// 从 pagemap 条目中提取物理页框号（PFN）
static uint64_t extract_pfn(uint64_t entry) {
    // 第63位：页面是否在内存中
    if (!(entry & (1ULL << 63))) {
        fprintf(stderr, "Page not present in memory (swapped or unmapped)\n");
        exit(EXIT_FAILURE);
    }
    // PFN 位于 bits 0~54
    return entry & ((1ULL << 55) - 1);
}

// 主转换函数
unsigned long virt_to_phys(pid_t pid, unsigned long vaddr, size_t page_size) {
    int fd = open_pagemap(pid);
    uint64_t entry = read_pagemap_entry(fd, vaddr, page_size);
    close(fd);

    uint64_t pfn = extract_pfn(entry);
    unsigned long page_offset = vaddr % page_size;
    return (pfn * page_size) + page_offset;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <pid> <virtual_address_hex> [page_size]\n", argv[0]);
        fprintf(stderr, "  Example: %s 1234 0x7fff12345000\n", argv[0]);
        return EXIT_FAILURE;
    }

    pid_t pid = (pid_t)atoi(argv[1]);
    unsigned long vaddr = strtoul(argv[2], NULL, 0);  // 自动识别 0x 前缀

    size_t page_size = get_page_size();
    if (argc >= 4) {
        long custom = atol(argv[3]);
        if (custom > 0) page_size = (size_t)custom;
    }

    unsigned long phys = virt_to_phys(pid, vaddr, page_size);
    printf("Virtual 0x%lx -> Physical 0x%lx (page size %zu)\n", vaddr, phys, page_size);

    return EXIT_SUCCESS;
}


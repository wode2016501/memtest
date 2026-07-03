#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <phys_addr> <length>\n", argv[0]);
        return 0;
    }
    // 1. 解析物理地址和长度
    off_t target_phys_addr = strtoul(argv[1], NULL, 0);
    size_t len = strtoul(argv[2], NULL, 0);

    // 2. 页对齐处理 (mmap要求偏移量是页大小的整数倍)
    size_t pagesize = sysconf(_SC_PAGE_SIZE);
    off_t page_base = (target_phys_addr / pagesize) * pagesize;
    off_t page_offset = target_phys_addr - page_base;

    // 3. 打开 /dev/mem 设备文件 (需要root权限)
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem failed");
        return -1;
    }
lseek(fd, target_phys_addr , SEEK_SET);
int value=0;
read(fd, &value, sizeof(value));
printf("Value at physical address 0x%lx: %d\n", target_phys_addr, value);
return 0;


    // 4. 使用mmap映射物理内存到当前进程空间
    unsigned char *mem = mmap(NULL, page_offset + len,
                              PROT_READ | PROT_WRITE,
                              MAP_SHARED,  // 注意：修改会同步回物理内存
                              fd, page_base);
    if (mem == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return -1;
    }

    // 5. 通过指针mem[page_offset + i]读写目标物理内存
    for (size_t i = 0; i < len; ++i) {
        printf("%02x ", (int)mem[page_offset + i]);
    }
    printf("\n");

    // 6. 清理
    munmap(mem, page_offset + len);
    close(fd);
    return 0;
}

// The parts of the Multiboot 1 information that GRUB passes to the kernel.

#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_MAGIC 0x2BADB002

#define MULTIBOOT_FLAG_CMDLINE (1u << 2)
#define MULTIBOOT_FLAG_MMAP    (1u << 6)

#define MULTIBOOT_MEMORY_AVAILABLE 1

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower, mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count, mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length, mmap_addr;
} __attribute__((packed));

struct multiboot_mmap_entry {
    uint32_t size;  // size of this entry, not counting this field
    uint64_t addr;
    uint64_t length;
    uint32_t type;
} __attribute__((packed));

#endif

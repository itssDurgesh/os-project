// Global Descriptor Table and Task State Segment.
// The GDT defines the kernel and user segments. The TSS tells the CPU which
// kernel stack to switch to when an interrupt arrives from ring 3.

#include "hal.h"
#include "kstring.h"

#define GDT_ENTRIES 6

#define ACCESS_KERNEL_CODE 0x9A
#define ACCESS_KERNEL_DATA 0x92
#define ACCESS_USER_CODE   0xFA
#define ACCESS_USER_DATA   0xF2
#define ACCESS_TSS         0x89

#define FLAGS_4K_32BIT 0xCF  // 4 KB limit units, 32-bit segment

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t flags_limit_high;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss {
    uint32_t previous;
    uint32_t esp0, ss0;  // kernel stack used on entry from ring 3
    uint32_t unused[22];
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

extern "C" void gdt_load(const struct gdt_pointer *pointer);

static struct gdt_entry gdt[GDT_ENTRIES];
static struct tss tss;

static void gdt_set(int index, uint32_t base, uint32_t limit,
                    uint8_t access, uint8_t flags)
{
    gdt[index].limit_low = limit & 0xFFFF;
    gdt[index].base_low = base & 0xFFFF;
    gdt[index].base_mid = (base >> 16) & 0xFF;
    gdt[index].access = access;
    gdt[index].flags_limit_high = (flags & 0xF0) | ((limit >> 16) & 0x0F);
    gdt[index].base_high = (base >> 24) & 0xFF;
}

void gdt_init()
{
    struct gdt_pointer pointer = {
        .limit = sizeof(gdt) - 1,
        .base = (uint32_t)gdt,
    };

    memset(&tss, 0, sizeof(tss));
    tss.ss0 = KERNEL_DATA_SELECTOR;
    tss.iomap_base = sizeof(tss);

    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFF, ACCESS_KERNEL_CODE, FLAGS_4K_32BIT);
    gdt_set(2, 0, 0xFFFFF, ACCESS_KERNEL_DATA, FLAGS_4K_32BIT);
    gdt_set(3, 0, 0xFFFFF, ACCESS_USER_CODE, FLAGS_4K_32BIT);
    gdt_set(4, 0, 0xFFFFF, ACCESS_USER_DATA, FLAGS_4K_32BIT);
    gdt_set(5, (uint32_t)&tss, sizeof(tss) - 1, ACCESS_TSS, 0);

    gdt_load(&pointer);
}

void tss_set_kernel_stack(uint32_t esp0)
{
    tss.esp0 = esp0;
}

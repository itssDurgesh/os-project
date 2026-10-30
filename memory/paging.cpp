/* Two-level x86 paging. A virtual address splits into a 10-bit page directory
 * index, a 10-bit page table index and a 12-bit offset. */

#include "console.h"
#include "hal.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"

#define PD_INDEX(virt) ((virt) >> 22)
#define PT_INDEX(virt) (((virt) >> 12) & 0x3FF)

#define VECTOR_PAGE_FAULT 14

uint32_t *kernel_page_dir;

static inline void invalidate_page(uint32_t virt)
{
    __asm__ volatile ("invlpg (%0)" : : "r"(virt));
}

static uint32_t *get_page_table(uint32_t *page_dir, uint32_t virt,
                                uint32_t flags, int create)
{
    uint32_t *entry = &page_dir[PD_INDEX(virt)];

    if ((*entry & PTE_PRESENT) == 0) {
        if (!create)
            return NULL;

        uint32_t table = alloc_frame();

        if (table == 0)
            return NULL;

        memset(phys_to_virt(table), 0, PAGE_SIZE);
        *entry = table | PTE_PRESENT | PTE_WRITABLE | (flags & PTE_USER);
    }

    return (uint32_t *)phys_to_virt(*entry & PAGE_MASK);
}

int paging_map(uint32_t *page_dir, uint32_t virt, uint32_t phys, uint32_t flags)
{
    uint32_t *table = get_page_table(page_dir, virt, flags, 1);

    if (table == NULL)
        return -1;

    table[PT_INDEX(virt)] = (phys & PAGE_MASK) | flags | PTE_PRESENT;
    invalidate_page(virt);
    return 0;
}

/* Removes a mapping and returns the physical address it pointed to. */
uint32_t paging_unmap(uint32_t *page_dir, uint32_t virt)
{
    uint32_t *table = get_page_table(page_dir, virt, 0, 0);
    uint32_t entry;

    if (table == NULL || (table[PT_INDEX(virt)] & PTE_PRESENT) == 0)
        return 0;

    entry = table[PT_INDEX(virt)];
    table[PT_INDEX(virt)] = 0;
    invalidate_page(virt);
    return entry & PAGE_MASK;
}

/* Returns the page table entry for an address, or 0 if there isn't one. */
uint32_t paging_lookup(const uint32_t *page_dir, uint32_t virt)
{
    uint32_t entry = page_dir[PD_INDEX(virt)];

    if ((entry & PTE_PRESENT) == 0)
        return 0;

    return ((const uint32_t *)phys_to_virt(entry & PAGE_MASK))[PT_INDEX(virt)];
}

void paging_switch(uint32_t *page_dir)
{
    write_cr3(virt_to_phys(page_dir));   /* this also flushes the TLB */
}

static void page_fault(struct Registers *r)
{
    /* A bad program only ends itself. A bad kernel can't be trusted to go on. */
    if (r->cs & 3)
        process_fault(r, "segmentation fault");

    kprintf("\npage fault at address %p, error %x\n", (void *)read_cr2(),
            r->error_code);
    panic_with_registers(r, "page fault in kernel");
}

void paging_init(void)
{
    uint32_t page_dir = alloc_frame();
    uint32_t memory_top = total_frames() * PAGE_SIZE;

    if (page_dir == 0)
        panic("no memory for the kernel page directory");

    kernel_page_dir = (uint32_t *)phys_to_virt(page_dir);
    memset(kernel_page_dir, 0, PAGE_SIZE);

    /* All RAM appears at KERNEL_VMA in every address space. These tables never
     * change, so each process can share them by copying the directory entries. */
    for (uint32_t phys = 0; phys < memory_top; phys += PAGE_SIZE) {
        if (paging_map(kernel_page_dir, (uint32_t)phys_to_virt(phys), phys, PTE_WRITABLE) != 0)
            panic("no memory for the kernel page tables");
    }

    paging_switch(kernel_page_dir);
    set_interrupt_handler(VECTOR_PAGE_FAULT, page_fault);
}

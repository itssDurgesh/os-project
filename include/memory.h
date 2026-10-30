// Memory Management: frames, paging, slab and kmalloc (owner: Memory).

#ifndef MEMORY_H
#define MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "abi.h"

struct multiboot_info;
struct Process;
struct Registers;
class Inode;

#define KERNEL_VMA 0xC0000000u
#define PAGE_SIZE  4096u
#define PAGE_MASK  (~(PAGE_SIZE - 1))

// The kernel maps all physical RAM at KERNEL_VMA, so it can reach any physical
// address by adding this offset.
#define phys_to_virt(address) ((void *)((uint32_t)(address) + KERNEL_VMA))
#define virt_to_phys(address) ((uint32_t)(address) - KERNEL_VMA)

#define round_up_to_page(value) (((uint32_t)(value) + PAGE_SIZE - 1) & PAGE_MASK)

#define PTE_PRESENT  0x1u
#define PTE_WRITABLE 0x2u
#define PTE_USER     0x4u
#define PTE_ACCESSED 0x20u   // the CPU sets this when the page is used
#define PTE_DIRTY    0x40u   // the CPU sets this when the page is written
#define PTE_SHARED   0x200u  // our own flag: fork must share this page, not copy it
#define PTE_SWAPPED  0x400u  // our own flag: the page is in swap

// Physical frames. Addresses are physical, and 0 means out of memory.
void frame_init(const struct multiboot_info *mbi);
uint32_t alloc_frame();
uint32_t alloc_frames(uint32_t count);  // physically contiguous
void free_frame(uint32_t address);
void free_frames(uint32_t address);  // frees one alloc_frames() block
void add_frame_user(uint32_t address);  // adds a user of a shared frame
uint32_t frame_user_count(uint32_t address);
bool frame_get_owner(uint32_t frame_number, uint32_t **page_dir, uint32_t *virt);
void frame_set_owner(uint32_t address, uint32_t *page_dir, uint32_t virt);
uint32_t total_frames();
uint32_t free_frame_count();

// Page tables
extern uint32_t *kernel_page_dir;

void paging_init();
void paging_switch(uint32_t *page_dir);
int paging_map(uint32_t *page_dir, uint32_t virt, uint32_t phys, uint32_t flags);
uint32_t paging_unmap(uint32_t *page_dir, uint32_t virt);
uint32_t paging_lookup(const uint32_t *page_dir, uint32_t virt);
uint32_t *paging_get_entry(uint32_t *page_dir, uint32_t virt);

// Per-process address spaces
uint32_t *make_page_directory();
uint32_t *copy_page_directory(const uint32_t *parent);
void delete_page_directory(uint32_t *page_dir);
int write_to_page_directory(uint32_t *page_dir, uint32_t virt, const void *source, uint32_t count);

// One part of a program's address space, such as its code, heap or stack. Pages
// are only loaded when they are first used.
#define MAX_MEMORY_AREAS 16

struct MemoryArea {
    bool in_use;
    uint32_t start;
    uint32_t end;
    uint32_t flags;             // PTE_ flags for its pages
    Inode *file;                // nullptr means the pages start as zeros
    uint32_t file_offset;
    uint32_t file_size;         // anything after this part is zeros
};

// Virtual memory: demand paging, swap and Clock
void virtual_memory_init();
void get_memory_info(MemoryInfo *info);
uint32_t get_user_frame();
bool handle_page_fault(uint32_t address, bool is_write);
int map_new_user_page(uint32_t *page_dir, uint32_t virt, uint32_t flags);
void unmap_user_page(uint32_t *page_dir, uint32_t virt);
void read_swapped_page(uint32_t slot, uint32_t frame);
void memory_page_returned_to_swap();
int add_memory_area(MemoryArea *areas, uint32_t start, uint32_t end, uint32_t flags,
                    Inode *file, uint32_t file_offset, uint32_t file_size);
void release_memory_areas(MemoryArea *areas);
void copy_memory_areas(const MemoryArea *from, MemoryArea *to);

// Checking addresses that user programs pass to system calls
bool user_range_valid(const void *address, uint32_t count, bool write);
int copy_string_from_user(char *destination, const char *source, uint32_t size);

// Slab allocator for fixed-size kernel objects
struct Slab;

struct SlabCache {
    size_t object_size;
    uint32_t objects_per_slab;
    Slab *partial;  // slabs with at least one free object
};

void slab_cache_init(SlabCache *cache, size_t object_size);
void *slab_alloc(SlabCache *cache);
void slab_free(void *object);

// General kernel allocator built on top of the slabs
void kmalloc_init();
void *kmalloc(size_t size);
void *kmalloc_zeroed(size_t size);
void kfree(void *pointer);

#endif

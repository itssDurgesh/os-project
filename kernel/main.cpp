// Kernel entry point. Each step starts what the next one needs, in the order
// given by the PRD's Integration section.

#include "console.h"
#include "hal.h"
#include "input.h"
#include "kstring.h"
#include "memory.h"
#include "multiboot.h"
#include "process.h"
#include "storage.h"
#include "syscall.h"
#include "tests.h"
#include "vfs.h"

#define COMMAND_LINE_LENGTH 128

static char command_line[COMMAND_LINE_LENGTH];

static void read_command_line(const struct multiboot_info *mbi)
{
    if (mbi->flags & MULTIBOOT_FLAG_CMDLINE)
        copy_string(command_line, (const char *)phys_to_virt(mbi->cmdline), COMMAND_LINE_LENGTH);
}

// PID 1 starts as this kernel thread, which can sleep while the disk works.
static void kernel_thread(void *argument)
{
    (void)argument;

    // Step 6: Device drivers
    keyboard_init();
    mouse_init();
    ata_init();

    // Step 7: Storage
    storage_init();

    // Step 8: File Handling
    vfs_set_root(ext2_mount(PARTITION_DATA));
    vfs_mount("/dev", devfs_mount());
    vfs_mount("/tmp", tmpfs_mount());
    vfs_mount("/proc", procfs_mount());

    if (test_mode_active()) {
        tests_run();
        return;
    }

    kprintf("OS Project: kernel running.\n");
    kprintf("%u MB RAM, %u MB free.\n", total_frames() / 256, free_frame_count() / 256);
    kprintf("BOOT-OK\n");

    // Step 9: Memory late stage, now that files and disk work.
    virtual_memory_init();

    // Step 10: System calls, then the first user program in ring 3.
    syscall_init();
    process_init();
    process_start_first("/sbin/init");
}

// In C++, a global object can have a constructor that must run before the kernel
// starts. A normal program has the C library to do this, but we have to do it.
typedef void (*Constructor)();

extern "C" Constructor start_of_constructors[];
extern "C" Constructor end_of_constructors[];

static void run_global_constructors()
{
    for (Constructor *item = start_of_constructors; item < end_of_constructors; item++)
        (*item)();
}

extern "C" void kmain(uint32_t magic, uint32_t mbi_physical)
{
    const struct multiboot_info *mbi =
        (const struct multiboot_info *)phys_to_virt(mbi_physical);

    run_global_constructors();

    // Step 1: Boot
    serial_init();
    vga_init();
    if (magic != MULTIBOOT_MAGIC)
        panic("not started by a Multiboot bootloader");
    read_command_line(mbi);
    tests_init(command_line);
    gdt_init();

    // Step 2: HAL (interrupts stay off)
    idt_init();
    pic_init();
    timer_init(TIMER_HZ);

    // Step 3: Memory core
    frame_init(mbi);
    paging_init();
    kmalloc_init();

    // Step 4: Sync primitives need no start-up work.

    // Step 5: Process core, then this code becomes the idle process.
    scheduler_init();
    thread_create("kernel", DEFAULT_PRIORITY, kernel_thread, nullptr);
    idle_loop();
}

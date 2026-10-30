/* Kernel entry point. Each step starts what the next one needs, in the order
 * given by the PRD's Integration section. */

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
        strlcpy(command_line, P2V(mbi->cmdline), COMMAND_LINE_LENGTH);
}

/* PID 1 starts as this kernel thread, which can sleep while the disk works. */
static void kernel_thread(void *argument)
{
    (void)argument;

    /* Step 6: Device drivers */
    keyboard_init();
    ata_init();

    /* Step 7: Storage */
    storage_init();

    /* Step 8: File Handling */
    vfs_init();
    vfs_set_root(ext2_mount(PARTITION_DATA));
    vfs_mount("/dev", devfs_mount());

    if (test_mode_active()) {
        tests_run();
        return;
    }

    kprintf("OS Project: kernel running.\n");
    kprintf("%u MB RAM, %u MB free.\n", frames_total() / 256, frames_free() / 256);
    kprintf("BOOT-OK\n");

    /* Step 10: System calls, then the first user program in ring 3. */
    syscall_init();
    process_init();
    process_start_first("/sbin/init");
}

void kmain(uint32_t magic, uint32_t mbi_physical)
{
    const struct multiboot_info *mbi = P2V(mbi_physical);

    /* Step 1: Boot */
    serial_init();
    vga_init();
    if (magic != MULTIBOOT_MAGIC)
        panic("not started by a Multiboot bootloader");
    read_command_line(mbi);
    tests_init(command_line);
    gdt_init();

    /* Step 2: HAL (interrupts stay off) */
    idt_init();
    pic_init();
    pit_init(TIMER_HZ);

    /* Step 3: Memory core */
    frame_init(mbi);
    paging_init();
    kmalloc_init();

    /* Step 4: Sync primitives need no start-up work. */

    /* Step 5: Process core, then this code becomes the idle process. */
    sched_init();
    thread_create("kernel", DEFAULT_PRIORITY, kernel_thread, NULL);
    sched_idle_loop();
}

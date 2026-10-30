// Boot-time self tests, one per milestone feature. Each prints TEST-PASS or
// TEST-FAIL, and tests/run.sh checks for it in the serial log.

#include <stddef.h>

#include "console.h"
#include "errno.h"
#include "hal.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"
#include "storage.h"
#include "sync.h"
#include "tests.h"
#include "vfs.h"

#define TEST_NAME_LENGTH 32

#define CHECK(condition)                                            \
    do {                                                            \
        if (!(condition)) {                                         \
            kprintf("TEST-FAIL: %s, line %d\n", #condition, __LINE__); \
            return;                                                 \
        }                                                           \
    } while (0)

static char test_name[TEST_NAME_LENGTH];

void tests_init(const char *command_line)
{
    static const char prefix[] = "test=";
    const char *found = command_line;

    for (; *found != '\0'; found++) {
        if (strncmp(found, prefix, sizeof(prefix) - 1) == 0) {
            int i = 0;

            found += sizeof(prefix) - 1;
            while (found[i] != '\0' && found[i] != ' ' && i < TEST_NAME_LENGTH - 1) {
                test_name[i] = found[i];
                i++;
            }
            test_name[i] = '\0';
            return;
        }
    }
}

bool test_mode_active()
{
    return test_name[0] != '\0';
}

// M1: the timer really is interrupting us.
static void test_ticks()
{
    uint32_t start = get_ticks();

    for (int count = 1; count <= 5; count++) {
        while (get_ticks() < start + count * 10)
            enable_interrupts_and_wait();
        kprintf("tick %d\n", count * 10);
    }
    kprintf("TEST-PASS\n");
}

// M1: a CPU exception reaches the panic handler.
static void test_panic()
{
    // Both are volatile, or GCC works out 1 / x without a divide instruction.
    volatile int number = 100;
    volatile int zero = 0;
    volatile int result = number / zero;

    (void)result;
    kprintf("TEST-FAIL: survived a division by zero\n");
}

// M2: frames, kmalloc and paging all work, and nothing leaks.
static void test_memory()
{
    uint32_t free_before = free_frame_count();
    uint32_t first = alloc_frame();
    uint32_t second = alloc_frame();
    uint32_t block;
    void *pointers[64];

    kprintf("frames: %u total, %u free\n", total_frames(), free_before);
    CHECK(total_frames() > 30000);
    CHECK(first != 0 && second != 0 && first != second);
    CHECK(free_frame_count() == free_before - 2);

    add_frame_user(first);
    CHECK(frame_user_count(first) == 2);
    free_frame(first);
    CHECK(free_frame_count() == free_before - 2);  // still has one user
    free_frame(first);
    free_frame(second);
    CHECK(free_frame_count() == free_before);

    block = alloc_frames(8);
    CHECK(block != 0);
    CHECK(free_frame_count() == free_before - 8);
    free_frames(block);
    CHECK(free_frame_count() == free_before);

    // Different sizes must not overlap: fill each one, then check them all.
    for (int i = 0; i < 64; i++) {
        size_t size = 8 + i * 300;

        pointers[i] = kmalloc(size);
        CHECK(pointers[i] != nullptr);
        memset(pointers[i], i + 1, size);
    }
    for (int i = 0; i < 64; i++) {
        size_t size = 8 + i * 300;

        for (size_t byte = 0; byte < size; byte++)
            CHECK(((unsigned char *)pointers[i])[byte] == i + 1);
        kfree(pointers[i]);
    }
    CHECK(free_frame_count() == free_before);

    // A mapped page must reach the same frame through both addresses.
    uint32_t test_virt = 0x40000000;
    uint32_t frame = alloc_frame();

    CHECK(paging_map(kernel_page_dir, test_virt, frame, PTE_WRITABLE) == 0);
    *(volatile uint32_t *)test_virt = 0x12345678;
    CHECK(*(volatile uint32_t *)phys_to_virt(frame) == 0x12345678);
    CHECK(paging_unmap(kernel_page_dir, test_virt) == frame);
    CHECK(paging_lookup(kernel_page_dir, test_virt) == 0);
    free_frame(frame);

    kprintf("TEST-PASS\n");
}

// M2: touching unmapped memory is caught, not silently ignored.
static void test_pagefault()
{
    volatile uint32_t *unmapped = (volatile uint32_t *)0x50000000;
    volatile uint32_t value = *unmapped;

    (void)value;
    kprintf("TEST-FAIL: read from unmapped memory worked\n");
}

// M3: two equal threads take turns under the timer.
#define TURN_LOG_SIZE 64

static volatile char turn_log[TURN_LOG_SIZE];
static volatile int turn_count;

static void turn_thread(void *argument)
{
    char name = (char)(uint32_t)argument;
    uint32_t start = get_ticks();

    while (get_ticks() - start < 30) {
        if (turn_count == 0 || turn_log[turn_count - 1] != name) {
            if (turn_count < TURN_LOG_SIZE - 1)
                {
                turn_log[turn_count] = name;
                turn_count = turn_count + 1;
            }
        }
        give_up_cpu_if_needed();
    }
}

static void test_sched()
{
    Process *a = thread_create("turn-a", 1, turn_thread, (void *)'A');
    Process *b = thread_create("turn-b", 1, turn_thread, (void *)'B');
    char log[TURN_LOG_SIZE];

    thread_join(a);
    thread_join(b);

    for (int i = 0; i < turn_count; i++)
        log[i] = turn_log[i];
    log[turn_count] = '\0';

    kprintf("turn order: %s\n", log);
    CHECK(turn_count >= 4);
    kprintf("TEST-PASS\n");
}

// M3: aging lets a low-priority thread run beside a busy high-priority one.
static volatile bool low_priority_ran;
static volatile bool stop_busy_thread;

static void busy_thread(void *argument)
{
    (void)argument;
    while (!stop_busy_thread)
        give_up_cpu_if_needed();
}

static void low_priority_thread(void *argument)
{
    (void)argument;
    low_priority_ran = true;
}

static void test_aging()
{
    Process *busy = thread_create("busy", 1, busy_thread, nullptr);
    Process *low = thread_create("low", 3, low_priority_thread, nullptr);
    uint32_t start = get_ticks();

    // Sleeping keeps this thread out of the way for 3 seconds at most.
    while (!low_priority_ran && get_ticks() - start < 300)
        thread_sleep(10);

    kprintf("low priority ran after %u ticks\n", get_ticks() - start);
    stop_busy_thread = true;
    thread_join(busy);
    thread_join(low);

    CHECK(low_priority_ran);
    kprintf("TEST-PASS\n");
}

// M3: locks really do protect shared data, and sleepers get woken.
#define WORKERS 4
#define ROUNDS 2000

static Mutex counter_lock;
static volatile int shared_counter;

static void counter_thread(void *argument)
{
    (void)argument;

    for (int i = 0; i < ROUNDS; i++) {
        counter_lock.lock();

        int value = shared_counter;

        give_up_cpu_if_needed();  // a bad lock would lose updates here
        shared_counter = value + 1;
        counter_lock.unlock();
    }
}

static Semaphore items;
static Semaphore slots;
static volatile int items_received;

static void producer_thread(void *argument)
{
    (void)argument;
    for (int i = 0; i < 50; i++) {
        slots.wait();
        items.post();
    }
}

static void consumer_thread(void *argument)
{
    (void)argument;
    for (int i = 0; i < 50; i++) {
        items.wait();
        items_received = items_received + 1;
        slots.post();
    }
}

static Mutex ready_lock;
static CondVar ready_signal;
static volatile bool ready_flag;
static volatile bool waiter_finished;

static void condvar_thread(void *argument)
{
    (void)argument;
    ready_lock.lock();
    while (!ready_flag)
        ready_signal.wait(ready_lock);
    ready_lock.unlock();
    waiter_finished = true;
}

static void test_sync()
{
    Process *workers[WORKERS];
    Process *producer;
    Process *consumer;
    Process *waiter;
    Spinlock spin;

    spin.lock();
    spin.unlock();

    for (int i = 0; i < WORKERS; i++)
        workers[i] = thread_create("counter", 1, counter_thread, nullptr);
    for (int i = 0; i < WORKERS; i++)
        thread_join(workers[i]);
    kprintf("counter: %d\n", shared_counter);
    CHECK(shared_counter == WORKERS * ROUNDS);

    items.set_count(0);
    slots.set_count(4);
    producer = thread_create("producer", 1, producer_thread, nullptr);
    consumer = thread_create("consumer", 1, consumer_thread, nullptr);
    thread_join(producer);
    thread_join(consumer);
    kprintf("items received: %d\n", items_received);
    CHECK(items_received == 50);

    waiter = thread_create("waiter", 1, condvar_thread, nullptr);
    thread_sleep(5);
    CHECK(!waiter_finished);  // it must still be waiting
    ready_lock.lock();
    ready_flag = true;
    ready_signal.signal();
    ready_lock.unlock();
    thread_join(waiter);
    CHECK(waiter_finished);

    kprintf("TEST-PASS\n");
}

// M4: partition table, ATA, buffer cache and ext2 reading all work together.
static void test_disk()
{
    static char text[64];
    Inode *inode;
    uint32_t reads_before;
    uint8_t *block;
    int length;

    CHECK(partition_blocks(PARTITION_KERNEL) == 16 * 256);
    CHECK(partition_blocks(PARTITION_SWAP) == 32 * 256);
    CHECK(partition_blocks(PARTITION_DATA) == 64 * 256);

    CHECK(vfs_lookup(nullptr, "/etc/hello.txt", &inode) == 0);
    length = inode->read(text, 0, sizeof(text) - 1);
    CHECK(length > 0);
    text[length] = '\0';
    kprintf("hello.txt: %s", text);
    CHECK(strcmp(text, "Hello from the OS Project disk.\n") == 0);

    // The second read of the same data must not touch the disk.
    reads_before = block_disk_reads();
    CHECK(inode->read(text, 0, sizeof(text) - 1) == length);
    kprintf("disk reads on the second read: %u\n",
            block_disk_reads() - reads_before);
    CHECK(block_disk_reads() == reads_before);
    release_inode(inode);

    // 5 MB reaches direct, indirect and double-indirect blocks.
    CHECK(vfs_lookup(nullptr, "/etc/big.bin", &inode) == 0);
    kprintf("big.bin: %u bytes\n", inode->size);
    CHECK(inode->size == 5 * 1024 * 1024);

    block = (uint8_t *)kmalloc(BLOCK_SIZE);
    CHECK(block != nullptr);
    for (uint32_t offset = 0; offset < inode->size; offset += BLOCK_SIZE) {
        CHECK(inode->read(block, offset, BLOCK_SIZE) == BLOCK_SIZE);
        for (uint32_t i = 0; i < BLOCK_SIZE; i++) {
            uint32_t position = offset + i;
            uint8_t expected = position ^ (position >> 8) ^ (position >> 16);

            CHECK(block[i] == expected);
        }
    }
    kfree(block);
    release_inode(inode);

    CHECK(vfs_lookup(nullptr, "/etc/missing.txt", &inode) == -ENOENT);
    kprintf("TEST-PASS\n");
}

void tests_run()
{
    static const struct {
        const char *name;
        void (*run)(void);
    } tests[] = {
        { "ticks", test_ticks },
        { "panic", test_panic },
        { "memory", test_memory },
        { "pagefault", test_pagefault },
        { "sched", test_sched },
        { "aging", test_aging },
        { "sync", test_sync },
        { "disk", test_disk },
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if (strcmp(test_name, tests[i].name) == 0) {
            tests[i].run();
            qemu_exit(TEST_EXIT_DONE);
            return;
        }
    }

    kprintf("TEST-FAIL: unknown test %s\n", test_name);
    qemu_exit(TEST_EXIT_DONE);
}

/* Boot-time self tests, one per milestone feature. Each prints TEST-PASS or
 * TEST-FAIL, and tests/run.sh checks for it in the serial log. */

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

bool test_mode_active(void)
{
    return test_name[0] != '\0';
}

/* M1: the timer really is interrupting us. */
static void test_ticks(void)
{
    uint32_t start = pit_ticks();

    for (int count = 1; count <= 5; count++) {
        while (pit_ticks() < start + count * 10)
            cpu_sti_halt();
        kprintf("tick %d\n", count * 10);
    }
    kprintf("TEST-PASS\n");
}

/* M1: a CPU exception reaches the panic handler. */
static void test_panic(void)
{
    /* Both are volatile, or GCC works out 1 / x without a divide instruction. */
    volatile int number = 100;
    volatile int zero = 0;
    volatile int result = number / zero;

    (void)result;
    kprintf("TEST-FAIL: survived a division by zero\n");
}

/* M2: frames, kmalloc and paging all work, and nothing leaks. */
static void test_memory(void)
{
    uint32_t free_before = frames_free();
    uint32_t first = alloc_frame();
    uint32_t second = alloc_frame();
    uint32_t block;
    void *pointers[64];

    kprintf("frames: %u total, %u free\n", frames_total(), free_before);
    CHECK(frames_total() > 30000);
    CHECK(first != 0 && second != 0 && first != second);
    CHECK(frames_free() == free_before - 2);

    frame_ref(first);
    CHECK(frame_refcount(first) == 2);
    free_frame(first);
    CHECK(frames_free() == free_before - 2);    /* still has one user */
    free_frame(first);
    free_frame(second);
    CHECK(frames_free() == free_before);

    block = alloc_frames(8);
    CHECK(block != 0);
    CHECK(frames_free() == free_before - 8);
    free_frames(block);
    CHECK(frames_free() == free_before);

    /* Different sizes must not overlap: fill each one, then check them all. */
    for (int i = 0; i < 64; i++) {
        size_t size = 8 + i * 300;

        pointers[i] = kmalloc(size);
        CHECK(pointers[i] != NULL);
        memset(pointers[i], i + 1, size);
    }
    for (int i = 0; i < 64; i++) {
        size_t size = 8 + i * 300;

        for (size_t byte = 0; byte < size; byte++)
            CHECK(((unsigned char *)pointers[i])[byte] == i + 1);
        kfree(pointers[i]);
    }
    CHECK(frames_free() == free_before);

    /* A mapped page must reach the same frame through both addresses. */
    uint32_t test_virt = 0x40000000;
    uint32_t frame = alloc_frame();

    CHECK(paging_map(kernel_page_dir, test_virt, frame, PTE_WRITABLE) == 0);
    *(volatile uint32_t *)test_virt = 0x12345678;
    CHECK(*(volatile uint32_t *)P2V(frame) == 0x12345678);
    CHECK(paging_unmap(kernel_page_dir, test_virt) == frame);
    CHECK(paging_lookup(kernel_page_dir, test_virt) == 0);
    free_frame(frame);

    kprintf("TEST-PASS\n");
}

/* M2: touching unmapped memory is caught, not silently ignored. */
static void test_pagefault(void)
{
    volatile uint32_t *unmapped = (volatile uint32_t *)0x50000000;
    volatile uint32_t value = *unmapped;

    (void)value;
    kprintf("TEST-FAIL: read from unmapped memory worked\n");
}

/* M3: two equal threads take turns under the timer. */
#define TURN_LOG_SIZE 64

static volatile char turn_log[TURN_LOG_SIZE];
static volatile int turn_count;

static void turn_thread(void *argument)
{
    char name = (char)(uint32_t)argument;
    uint32_t start = pit_ticks();

    while (pit_ticks() - start < 30) {
        if (turn_count == 0 || turn_log[turn_count - 1] != name) {
            if (turn_count < TURN_LOG_SIZE - 1)
                turn_log[turn_count++] = name;
        }
        sched_checkpoint();
    }
}

static void test_sched(void)
{
    struct pcb *a = thread_create("turn-a", 1, turn_thread, (void *)'A');
    struct pcb *b = thread_create("turn-b", 1, turn_thread, (void *)'B');
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

/* M3: aging lets a low-priority thread run beside a busy high-priority one. */
static volatile bool low_priority_ran;
static volatile bool stop_busy_thread;

static void busy_thread(void *argument)
{
    (void)argument;
    while (!stop_busy_thread)
        sched_checkpoint();
}

static void low_priority_thread(void *argument)
{
    (void)argument;
    low_priority_ran = true;
}

static void test_aging(void)
{
    struct pcb *busy = thread_create("busy", 1, busy_thread, NULL);
    struct pcb *low = thread_create("low", 3, low_priority_thread, NULL);
    uint32_t start = pit_ticks();

    /* Sleeping keeps this thread out of the way for 3 seconds at most. */
    while (!low_priority_ran && pit_ticks() - start < 300)
        thread_sleep(10);

    kprintf("low priority ran after %u ticks\n", pit_ticks() - start);
    stop_busy_thread = true;
    thread_join(busy);
    thread_join(low);

    CHECK(low_priority_ran);
    kprintf("TEST-PASS\n");
}

/* M3: locks really do protect shared data, and sleepers get woken. */
#define WORKERS 4
#define ROUNDS 2000

static struct mutex counter_lock;
static volatile int shared_counter;

static void counter_thread(void *argument)
{
    (void)argument;

    for (int i = 0; i < ROUNDS; i++) {
        mutex_lock(&counter_lock);

        int value = shared_counter;

        sched_checkpoint();         /* a bad lock would lose updates here */
        shared_counter = value + 1;
        mutex_unlock(&counter_lock);
    }
}

static struct semaphore items;
static struct semaphore slots;
static volatile int items_received;

static void producer_thread(void *argument)
{
    (void)argument;
    for (int i = 0; i < 50; i++) {
        semaphore_wait(&slots);
        semaphore_post(&items);
    }
}

static void consumer_thread(void *argument)
{
    (void)argument;
    for (int i = 0; i < 50; i++) {
        semaphore_wait(&items);
        items_received++;
        semaphore_post(&slots);
    }
}

static struct mutex ready_lock;
static struct condvar ready_signal;
static volatile bool ready_flag;
static volatile bool waiter_finished;

static void condvar_thread(void *argument)
{
    (void)argument;
    mutex_lock(&ready_lock);
    while (!ready_flag)
        condvar_wait(&ready_signal, &ready_lock);
    mutex_unlock(&ready_lock);
    waiter_finished = true;
}

static void test_sync(void)
{
    struct pcb *workers[WORKERS];
    struct pcb *producer;
    struct pcb *consumer;
    struct pcb *waiter;
    struct spinlock spin;

    spinlock_init(&spin);
    spin_lock(&spin);
    spin_unlock(&spin);

    mutex_init(&counter_lock);
    for (int i = 0; i < WORKERS; i++)
        workers[i] = thread_create("counter", 1, counter_thread, NULL);
    for (int i = 0; i < WORKERS; i++)
        thread_join(workers[i]);
    kprintf("counter: %d\n", shared_counter);
    CHECK(shared_counter == WORKERS * ROUNDS);

    semaphore_init(&items, 0);
    semaphore_init(&slots, 4);
    producer = thread_create("producer", 1, producer_thread, NULL);
    consumer = thread_create("consumer", 1, consumer_thread, NULL);
    thread_join(producer);
    thread_join(consumer);
    kprintf("items received: %d\n", items_received);
    CHECK(items_received == 50);

    mutex_init(&ready_lock);
    condvar_init(&ready_signal);
    waiter = thread_create("waiter", 1, condvar_thread, NULL);
    thread_sleep(5);
    CHECK(!waiter_finished);        /* it must still be waiting */
    mutex_lock(&ready_lock);
    ready_flag = true;
    condvar_signal(&ready_signal);
    mutex_unlock(&ready_lock);
    thread_join(waiter);
    CHECK(waiter_finished);

    kprintf("TEST-PASS\n");
}

/* M4: partition table, ATA, buffer cache and ext2 reading all work together. */
static void test_disk(void)
{
    static char text[64];
    struct inode *inode;
    uint32_t reads_before;
    uint8_t *block;
    int length;

    CHECK(partition_blocks(PARTITION_KERNEL) == 16 * 256);
    CHECK(partition_blocks(PARTITION_SWAP) == 32 * 256);
    CHECK(partition_blocks(PARTITION_DATA) == 64 * 256);

    CHECK(vfs_lookup(NULL, "/etc/hello.txt", &inode) == 0);
    length = inode->ops->read(inode, text, 0, sizeof(text) - 1);
    CHECK(length > 0);
    text[length] = '\0';
    kprintf("hello.txt: %s", text);
    CHECK(strcmp(text, "Hello from the OS Project disk.\n") == 0);

    /* The second read of the same data must not touch the disk. */
    reads_before = block_disk_reads();
    CHECK(inode->ops->read(inode, text, 0, sizeof(text) - 1) == length);
    kprintf("disk reads on the second read: %u\n",
            block_disk_reads() - reads_before);
    CHECK(block_disk_reads() == reads_before);
    inode_put(inode);

    /* 5 MB reaches direct, indirect and double-indirect blocks. */
    CHECK(vfs_lookup(NULL, "/etc/big.bin", &inode) == 0);
    kprintf("big.bin: %u bytes\n", inode->size);
    CHECK(inode->size == 5 * 1024 * 1024);

    block = kmalloc(BLOCK_SIZE);
    CHECK(block != NULL);
    for (uint32_t offset = 0; offset < inode->size; offset += BLOCK_SIZE) {
        CHECK(inode->ops->read(inode, block, offset, BLOCK_SIZE) == BLOCK_SIZE);
        for (uint32_t i = 0; i < BLOCK_SIZE; i++) {
            uint32_t position = offset + i;
            uint8_t expected = position ^ (position >> 8) ^ (position >> 16);

            CHECK(block[i] == expected);
        }
    }
    kfree(block);
    inode_put(inode);

    CHECK(vfs_lookup(NULL, "/etc/missing.txt", &inode) == -ENOENT);
    kprintf("TEST-PASS\n");
}

void tests_run(void)
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

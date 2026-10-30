// ATA disk driver, primary channel, master drive, using programmed I/O (PIO).
// The process sleeps until the disk raises IRQ 14, so waiting costs no CPU time.
// DMA is a stretch goal and would sit behind these same two functions.

#include "console.h"
#include "errno.h"
#include "hal.h"
#include "process.h"
#include "storage.h"
#include "sync.h"

#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECTORS     0x1F2
#define ATA_LBA_LOW     0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HIGH    0x1F5
#define ATA_DRIVE       0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7

#define STATUS_ERROR    0x01
#define STATUS_DRQ      0x08  // the drive is ready to transfer data
#define STATUS_BUSY     0x80

#define COMMAND_READ    0x20
#define COMMAND_WRITE   0x30
#define COMMAND_FLUSH   0xE7

#define DRIVE_MASTER_LBA 0xE0

#define WORDS_PER_SECTOR 256
#define ATA_IRQ 14

static Mutex ata_lock;
static WaitQueue irq_waiters;
static volatile bool irq_arrived;

static void ata_interrupt(Registers *r)
{
    (void)r;
    inb(ATA_STATUS);  // reading the status clears the interrupt
    irq_arrived = true;
    irq_waiters.wake_all();
}

static void wait_for_irq()
{
    uint32_t flags = turn_off_interrupts();

    while (!irq_arrived)
        irq_waiters.sleep();
    irq_arrived = false;

    restore_interrupts(flags);
}

static void wait_not_busy()
{
    while (inb(ATA_STATUS) & STATUS_BUSY)
        ;
}

static int wait_for_data()
{
    uint8_t status;

    do {
        status = inb(ATA_STATUS);
    } while ((status & STATUS_BUSY) || !(status & (STATUS_DRQ | STATUS_ERROR)));

    return (status & STATUS_ERROR) ? -EIO : 0;
}

static void send_command(uint32_t lba, uint8_t sectors, uint8_t command)
{
    wait_not_busy();
    irq_arrived = false;

    outb(ATA_DRIVE, DRIVE_MASTER_LBA | ((lba >> 24) & 0x0F));
    outb(ATA_SECTORS, sectors);
    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(ATA_COMMAND, command);
}

int ata_read(uint32_t lba, uint32_t sectors, void *buffer)
{
    uint8_t *destination = (uint8_t *)buffer;
    int result = 0;

    ata_lock.lock();
    send_command(lba, sectors, COMMAND_READ);

    for (uint32_t i = 0; i < sectors && result == 0; i++) {
        wait_for_irq();
        result = wait_for_data();
        if (result == 0)
            insw(ATA_DATA, destination + i * SECTOR_SIZE, WORDS_PER_SECTOR);
    }

    ata_lock.unlock();
    return result;
}

int ata_write(uint32_t lba, uint32_t sectors, const void *buffer)
{
    const uint8_t *source = (const uint8_t *)buffer;
    int result = 0;

    ata_lock.lock();
    send_command(lba, sectors, COMMAND_WRITE);

    for (uint32_t i = 0; i < sectors && result == 0; i++) {
        result = wait_for_data();
        if (result == 0) {
            outsw(ATA_DATA, source + i * SECTOR_SIZE, WORDS_PER_SECTOR);
            wait_for_irq();
        }
    }

    if (result == 0) {
        send_command(lba, 0, COMMAND_FLUSH);
        wait_for_irq();
    }

    ata_lock.unlock();
    return result;
}

void ata_init()
{
    set_interrupt_handler(VECTOR_IRQ_BASE + ATA_IRQ, ata_interrupt);
    pic_unmask(ATA_IRQ);
}

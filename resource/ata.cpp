// ATA disk driver, primary channel, master drive.
//
// With DMA, the disk controller copies the data to RAM by itself and the CPU only
// hears about it once, when everything is finished. If there is no DMA controller
// we fall back to PIO, where the CPU copies each sector itself.
// Either way the process sleeps until IRQ 14, so waiting costs no CPU time.

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

#define COMMAND_READ_DMA  0xC8
#define COMMAND_WRITE_DMA 0xCA

// The DMA controller, which PCI calls the "bus master".
#define PCI_CLASS_STORAGE 0x01
#define PCI_SUBCLASS_IDE  0x01
#define PCI_COMMAND_REGISTER 0x04
#define PCI_BAR4_REGISTER 0x20
#define PCI_ENABLE_BUS_MASTER 0x04

#define DMA_COMMAND 0           // offsets from dma_ports
#define DMA_STATUS  2
#define DMA_TABLE   4

#define DMA_START        0x01
#define DMA_TO_MEMORY    0x08
#define DMA_ERROR        0x02
#define DMA_CLEAR_STATUS 0x06
#define DMA_LAST_ENTRY   0x8000

// One entry of the list that tells the controller where the memory is.
struct DmaTableEntry {
    uint32_t physical_address;
    uint16_t byte_count;
    uint16_t end_flag;
} __attribute__((packed));

static DmaTableEntry dma_table __attribute__((aligned(16)));
static uint16_t dma_ports = 0;  // 0 means we have no DMA and use PIO

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

static int pio_read(uint32_t lba, uint32_t sectors, void *buffer)
{
    uint8_t *destination = (uint8_t *)buffer;
    int result = 0;

    send_command(lba, sectors, COMMAND_READ);

    for (uint32_t i = 0; i < sectors && result == 0; i++) {
        wait_for_irq();
        result = wait_for_data();
        if (result == 0)
            insw(ATA_DATA, destination + i * SECTOR_SIZE, WORDS_PER_SECTOR);
    }

    return result;
}

static int pio_write(uint32_t lba, uint32_t sectors, const void *buffer)
{
    const uint8_t *source = (const uint8_t *)buffer;
    int result = 0;

    send_command(lba, sectors, COMMAND_WRITE);

    for (uint32_t i = 0; i < sectors && result == 0; i++) {
        result = wait_for_data();
        if (result == 0) {
            outsw(ATA_DATA, source + i * SECTOR_SIZE, WORDS_PER_SECTOR);
            wait_for_irq();
        }
    }

    return result;
}

// `buffer` must be in the kernel's own memory, because the controller needs its
// physical address. Every caller uses a page or a kmalloc block, so this is true.
static int dma_transfer(uint32_t lba, uint32_t sectors, const void *buffer, bool is_write)
{
    dma_table.physical_address = virt_to_phys(buffer);
    dma_table.byte_count = sectors * SECTOR_SIZE;
    dma_table.end_flag = DMA_LAST_ENTRY;

    outb(dma_ports + DMA_COMMAND, 0);
    outl(dma_ports + DMA_TABLE, virt_to_phys(&dma_table));
    outb(dma_ports + DMA_STATUS, inb(dma_ports + DMA_STATUS) | DMA_CLEAR_STATUS);
    outb(dma_ports + DMA_COMMAND, is_write ? 0 : DMA_TO_MEMORY);

    send_command(lba, sectors, is_write ? COMMAND_WRITE_DMA : COMMAND_READ_DMA);
    outb(dma_ports + DMA_COMMAND, inb(dma_ports + DMA_COMMAND) | DMA_START);

    wait_for_irq();

    outb(dma_ports + DMA_COMMAND, inb(dma_ports + DMA_COMMAND) & ~DMA_START);

    uint8_t status = inb(dma_ports + DMA_STATUS);

    outb(dma_ports + DMA_STATUS, status | DMA_CLEAR_STATUS);

    if ((status & DMA_ERROR) || (inb(ATA_STATUS) & STATUS_ERROR))
        return -EIO;
    return 0;
}

int ata_read(uint32_t lba, uint32_t sectors, void *buffer)
{
    int result;

    ata_lock.lock();
    if (dma_ports != 0)
        result = dma_transfer(lba, sectors, buffer, false);
    else
        result = pio_read(lba, sectors, buffer);
    ata_lock.unlock();
    return result;
}

int ata_write(uint32_t lba, uint32_t sectors, const void *buffer, bool flush)
{
    int result;

    ata_lock.lock();
    if (dma_ports != 0)
        result = dma_transfer(lba, sectors, buffer, true);
    else
        result = pio_write(lba, sectors, buffer);

    if (result == 0 && flush) {
        send_command(lba, 0, COMMAND_FLUSH);
        wait_for_irq();
    }
    ata_lock.unlock();
    return result;
}

static void find_dma_controller()
{
    PciLocation controller;

    if (!pci_find_device(PCI_CLASS_STORAGE, PCI_SUBCLASS_IDE, &controller)) {
        kprintf("Disk: using PIO\n");
        return;
    }

    // This setting lets the controller use RAM by itself.
    pci_write(&controller, PCI_COMMAND_REGISTER,
              pci_read(&controller, PCI_COMMAND_REGISTER) | PCI_ENABLE_BUS_MASTER);

    dma_ports = pci_read(&controller, PCI_BAR4_REGISTER) & 0xFFFC;
    kprintf("Disk: using DMA\n");
}

void ata_init()
{
    set_interrupt_handler(VECTOR_IRQ_BASE + ATA_IRQ, ata_interrupt);
    pic_unmask(ATA_IRQ);
    find_dma_controller();
}

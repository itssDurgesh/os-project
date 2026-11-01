// Hardware Abstraction Layer (owner: Integration).
// Everything that touches the CPU or a chip directly is reached through here.

#ifndef HAL_H
#define HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TIMER_HZ 100  // one tick every 10 ms

#define KERNEL_CODE_SELECTOR 0x08
#define KERNEL_DATA_SELECTOR 0x10
#define USER_CODE_SELECTOR   0x1B
#define USER_DATA_SELECTOR   0x23

#define VECTOR_IRQ_BASE 0x20  // IRQ 0 after the PIC is remapped
#define VECTOR_SYSCALL  0x80

// CPU state saved on the stack by common_interrupt_code. Order matches hal/isr.asm.
struct Registers {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_ignored, ebx, edx, ecx, eax;
    uint32_t vector, error_code;
    uint32_t eip, cs, eflags;
    uint32_t user_esp, user_ss;  // only pushed when coming from ring 3
};

typedef void (*interrupt_handler_t)(Registers *r);

// Port I/O
static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

// Reads or writes `count` 16-bit words through one port, for the disk.
static inline void insw(uint16_t port, void *buffer, uint32_t count)
{
    __asm__ volatile ("rep insw" : "+D"(buffer), "+c"(count) : "d"(port) : "memory");
}

static inline void outsw(uint16_t port, const void *buffer, uint32_t count)
{
    __asm__ volatile ("rep outsw" : "+S"(buffer), "+c"(count) : "d"(port) : "memory");
}

// Interrupt control
static inline void disable_interrupts()  { __asm__ volatile ("cli"); }
static inline void halt_cpu() { __asm__ volatile ("hlt"); }

// Turns interrupts on and sleeps until one arrives. `sti` delays by one
// instruction, so no interrupt can slip in before the `hlt`.
static inline void enable_interrupts_and_wait() { __asm__ volatile ("sti; hlt"); }

static inline void enable_interrupts() { __asm__ volatile ("sti"); }

// Disables interrupts and returns the previous state for restore_interrupts().
static inline uint32_t turn_off_interrupts()
{
    uint32_t flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags));
    return flags;
}

static inline void restore_interrupts(uint32_t flags)
{
    __asm__ volatile ("push %0; popf" : : "r"(flags) : "cc");
}

static inline uint32_t read_cr2()
{
    uint32_t value;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(value));
    return value;
}

static inline void write_cr3(uint32_t value)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(value));
}

// GDT and TSS
void gdt_init();
void tss_set_kernel_stack(uint32_t esp0);

// IDT and interrupt dispatch
void idt_init();
void set_interrupt_handler(uint8_t vector, interrupt_handler_t handler);

// PIC
void pic_init();
void pic_unmask(uint8_t irq);
void pic_eoi(uint8_t irq);

// Timer
void timer_init(uint32_t hz);
uint32_t get_ticks();

// Real-time clock
struct RealTime {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
};

void rtc_read(RealTime *time);

// PCI
struct PciLocation {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
};

uint32_t pci_read(const PciLocation *where, uint8_t offset);
void pci_write(const PciLocation *where, uint8_t offset, uint32_t value);
bool pci_find_device(uint8_t class_code, uint8_t subclass, PciLocation *result);

// Serial port
void serial_init();
void serial_putc(char c);
void serial_enable_input(void (*callback)(char c));

// Ends a QEMU test run. Does nothing when the test device isn't attached.
void qemu_exit(uint8_t code);

#endif

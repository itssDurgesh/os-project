/* Milestone 0 kernel: prove it boots by printing to the screen and to serial. */

#include <stdint.h>

#define VGA_TEXT    ((volatile uint16_t *)0xB8000)
#define VGA_COLUMNS 80
#define VGA_WHITE   0x0F00

#define COM1 0x3F8

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

static void serial_init(void)
{
    outb(COM1 + 1, 0x00);   /* no serial interrupts */
    outb(COM1 + 3, 0x80);   /* unlock the baud rate divisor */
    outb(COM1 + 0, 0x03);   /* divisor 3 = 38400 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8 data bits, no parity, 1 stop bit */
    outb(COM1 + 2, 0xC7);   /* enable and clear the FIFOs */
}

static void serial_write(const char *text)
{
    for (; *text != '\0'; text++) {
        while ((inb(COM1 + 5) & 0x20) == 0)
            ;               /* wait until the transmitter is ready */
        outb(COM1, (uint8_t)*text);
    }
}

static void vga_write(int row, const char *text)
{
    volatile uint16_t *cell = VGA_TEXT + row * VGA_COLUMNS;

    for (; *text != '\0'; text++)
        *cell++ = VGA_WHITE | (uint8_t)*text;
}

static void vga_clear(void)
{
    for (int i = 0; i < VGA_COLUMNS * 25; i++)
        VGA_TEXT[i] = VGA_WHITE | ' ';
}

void kmain(void)
{
    serial_init();
    vga_clear();

    vga_write(0, "OS Project: hello");
    vga_write(2, "Milestone 0 passed. Close this window, then send boot.log to Integration.");

    serial_write("OS Project: hello\r\n");
    serial_write("BOOT-OK\r\n");
}

/* COM1 serial port. Everything the kernel prints also goes here, which is what
 * ends up in boot.log. */

#include "hal.h"

#define COM1 0x3F8

#define COM1_IRQ 4

#define LINE_STATUS 5
#define DATA_READY 0x01
#define TRANSMIT_READY 0x20

#define QEMU_EXIT_PORT 0xF4

void serial_init(void)
{
    outb(COM1 + 1, 0x00);   /* no serial interrupts */
    outb(COM1 + 3, 0x80);   /* unlock the baud rate divisor */
    outb(COM1 + 0, 0x03);   /* divisor 3 = 38400 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8 data bits, no parity, 1 stop bit */
    outb(COM1 + 2, 0xC7);   /* enable and clear the FIFOs */
}

void serial_putc(char c)
{
    while ((inb(COM1 + LINE_STATUS) & TRANSMIT_READY) == 0)
        ;
    outb(COM1, (uint8_t)c);
}

static void (*input_callback)(char c);

static void serial_interrupt(struct regs *r)
{
    (void)r;
    while (inb(COM1 + LINE_STATUS) & DATA_READY)
        input_callback((char)inb(COM1));
}

/* Typing in the serial terminal works like typing on the keyboard. */
void serial_enable_input(void (*callback)(char c))
{
    input_callback = callback;
    interrupt_register(VECTOR_IRQ_BASE + COM1_IRQ, serial_interrupt);
    outb(COM1 + 4, 0x0B);   /* the chip needs this to send interrupts at all */
    outb(COM1 + 1, 0x01);   /* interrupt when a byte arrives */
    pic_unmask(COM1_IRQ);
}

void qemu_exit(uint8_t code)
{
    outb(QEMU_EXIT_PORT, code);
}

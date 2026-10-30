/* The two 8259 Programmable Interrupt Controllers.
 * By default IRQ 0 arrives on vector 8, which is the CPU's double fault, so the
 * IRQs are moved to vectors 0x20-0x2F before interrupts are turned on. */

#include "hal.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define PIC_EOI      0x20
#define ICW1_INIT    0x11       /* start setup, ICW4 will follow */
#define ICW4_8086    0x01

#define PIC2_VECTOR_BASE (VECTOR_IRQ_BASE + 8)
#define CASCADE_IRQ      2

void pic_init(void)
{
    outb(PIC1_COMMAND, ICW1_INIT);
    outb(PIC2_COMMAND, ICW1_INIT);
    outb(PIC1_DATA, VECTOR_IRQ_BASE);
    outb(PIC2_DATA, PIC2_VECTOR_BASE);
    outb(PIC1_DATA, 1 << CASCADE_IRQ);  /* the second PIC is on IRQ 2 */
    outb(PIC2_DATA, CASCADE_IRQ);
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    /* Every IRQ stays masked until its driver asks for it. */
    outb(PIC1_DATA, (uint8_t)~(1 << CASCADE_IRQ));
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask(uint8_t irq)
{
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = (irq < 8) ? irq : irq - 8;

    outb(port, inb(port) & (uint8_t)~(1 << bit));
}

void pic_eoi(uint8_t irq)
{
    if (irq >= 8)
        outb(PIC2_COMMAND, PIC_EOI);
    outb(PIC1_COMMAND, PIC_EOI);
}

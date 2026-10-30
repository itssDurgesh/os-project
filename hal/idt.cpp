/* Interrupt Descriptor Table and the common interrupt dispatcher. */

#include "console.h"
#include "hal.h"
#include "process.h"

#define IDT_ENTRIES 256

#define GATE_KERNEL 0x8E        /* interrupt gate, ring 0 only */
#define GATE_USER   0xEE        /* interrupt gate, callable from ring 3 */

#define FIRST_IRQ_VECTOR VECTOR_IRQ_BASE
#define LAST_IRQ_VECTOR  (VECTOR_IRQ_BASE + 15)

struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

extern "C" uint32_t interrupt_stubs[IDT_ENTRIES];

static struct idt_entry idt[IDT_ENTRIES];
static interrupt_handler_t handlers[IDT_ENTRIES];

static void idt_set(int vector, uint32_t base, uint8_t flags)
{
    idt[vector].base_low = base & 0xFFFF;
    idt[vector].selector = KERNEL_CODE_SELECTOR;
    idt[vector].zero = 0;
    idt[vector].flags = flags;
    idt[vector].base_high = (base >> 16) & 0xFFFF;
}

void idt_init(void)
{
    struct idt_pointer pointer = {
        .limit = sizeof(idt) - 1,
        .base = (uint32_t)idt,
    };

    for (int vector = 0; vector < IDT_ENTRIES; vector++)
        idt_set(vector, interrupt_stubs[vector], GATE_KERNEL);

    idt_set(VECTOR_SYSCALL, interrupt_stubs[VECTOR_SYSCALL], GATE_USER);

    __asm__ volatile ("lidt %0" : : "m"(pointer));
}

void set_interrupt_handler(uint8_t vector, interrupt_handler_t handler)
{
    handlers[vector] = handler;
}

/* Called by common_interrupt_code for every interrupt, with interrupts off. */
extern "C" void handle_interrupt(struct Registers *r)
{
    uint32_t vector = r->vector;

    if (vector >= FIRST_IRQ_VECTOR && vector <= LAST_IRQ_VECTOR)
        pic_eoi(vector - FIRST_IRQ_VECTOR);

    if (handlers[vector] != NULL) {
        handlers[vector](r);
    } else if (vector < FIRST_IRQ_VECTOR) {
        if (r->cs & 3)
            process_fault(r, "CPU exception");
        panic_with_registers(r, "unhandled CPU exception");
    }

    /* The kernel isn't preempted, so a switch only happens on the way back
     * to user mode. */
    if (r->cs & 3)
        switch_process_if_needed();
}

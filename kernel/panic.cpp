/* Kernel panic: print what went wrong and stop, because carrying on could
 * corrupt data. */

#include "console.h"
#include "hal.h"
#include "tests.h"

static void halt_forever(void) __attribute__((noreturn));

static void halt_forever(void)
{
    if (test_mode_active())
        qemu_exit(TEST_EXIT_PANIC);

    for (;;) {
        disable_interrupts();
        halt_cpu();
    }
}

void panic(const char *format, ...)
{
    va_list args;

    disable_interrupts();
    kprintf("\nKERNEL PANIC: ");

    va_start(args, format);
    kvprintf(format, args);
    va_end(args);

    kprintf("\nSystem halted.\n");
    halt_forever();
}

void panic_with_registers(const struct Registers *r, const char *reason)
{
    disable_interrupts();
    kprintf("\nKERNEL PANIC: %s\n", reason);
    kprintf("vector=%u error=%x eip=%08x cs=%x eflags=%08x\n",
            r->vector, r->error_code, r->eip, r->cs, r->eflags);
    kprintf("eax=%08x ebx=%08x ecx=%08x edx=%08x\n",
            r->eax, r->ebx, r->ecx, r->edx);
    kprintf("esi=%08x edi=%08x ebp=%08x\n", r->esi, r->edi, r->ebp);
    kprintf("System halted.\n");
    halt_forever();
}

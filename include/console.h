// Kernel text output and panic (owner: Integration).

#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdarg.h>

struct Registers;

// VGA text driver (owner: Resource Management)
void vga_init();
void vga_putc(char c);

// Prints to the serial port and the screen. Supports %d %u %x %p %s %c %%,
// plus a width with optional zero fill, such as %08x.
void kprintf(const char *format, ...) __attribute__((format(printf, 1, 2)));
void kvprintf(const char *format, va_list args);

// Stops the kernel for an error it can't recover from.
void panic(const char *format, ...)
    __attribute__((format(printf, 1, 2), noreturn));

// Prints the saved CPU state, then panics.
void panic_with_registers(const Registers *r, const char *reason) __attribute__((noreturn));

#endif

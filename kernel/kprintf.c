/* kprintf: the kernel's printf. Output goes to the serial port and the screen. */

#include "console.h"
#include "hal.h"

static void put_char(char c)
{
    if (c == '\n')
        serial_putc('\r');
    serial_putc(c);
    vga_putc(c);
}

static void put_string(const char *text)
{
    for (; *text != '\0'; text++)
        put_char(*text);
}

static void put_number(uint32_t value, uint32_t base, int width, char fill)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[32];
    int length = 0;

    do {
        buffer[length++] = digits[value % base];
        value /= base;
    } while (value != 0);

    while (length < width) {
        put_char(fill);
        width--;
    }

    while (length > 0)
        put_char(buffer[--length]);
}

void kvprintf(const char *format, va_list args)
{
    for (; *format != '\0'; format++) {
        char fill = ' ';
        int width = 0;

        if (*format != '%') {
            put_char(*format);
            continue;
        }

        format++;
        if (*format == '0') {
            fill = '0';
            format++;
        }
        while (*format >= '0' && *format <= '9')
            width = width * 10 + (*format++ - '0');

        switch (*format) {
        case 'd': {
            int value = va_arg(args, int);
            if (value < 0) {
                put_char('-');
                put_number(-(uint32_t)value, 10, width, fill);
            } else {
                put_number(value, 10, width, fill);
            }
            break;
        }
        case 'u':
            put_number(va_arg(args, uint32_t), 10, width, fill);
            break;
        case 'x':
            put_number(va_arg(args, uint32_t), 16, width, fill);
            break;
        case 'p':
            put_string("0x");
            put_number((uint32_t)va_arg(args, void *), 16, 8, '0');
            break;
        case 's':
            put_string(va_arg(args, const char *));
            break;
        case 'c':
            put_char((char)va_arg(args, int));
            break;
        case '%':
            put_char('%');
            break;
        default:
            put_char('%');
            put_char(*format);
            break;
        }
    }
}

void kprintf(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    kvprintf(format, args);
    va_end(args);
}

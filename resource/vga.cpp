/* VGA text mode driver: 80x25 characters at physical address 0xB8000. */

#include "console.h"
#include "hal.h"
#include "kstring.h"
#include "memory.h"

#define VGA_COLUMNS 80
#define VGA_ROWS    25
#define VGA_WHITE   0x0F00

#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA  0x3D5
#define CURSOR_HIGH    0x0E
#define CURSOR_LOW     0x0F

static volatile uint16_t *const vga_text = (volatile uint16_t *)phys_to_virt(0xB8000);

static int column;
static int row;

static void update_cursor(void)
{
    uint16_t position = row * VGA_COLUMNS + column;

    outb(VGA_CRTC_INDEX, CURSOR_HIGH);
    outb(VGA_CRTC_DATA, position >> 8);
    outb(VGA_CRTC_INDEX, CURSOR_LOW);
    outb(VGA_CRTC_DATA, position & 0xFF);
}

static void scroll(void)
{
    for (int i = 0; i < VGA_COLUMNS * (VGA_ROWS - 1); i++)
        vga_text[i] = vga_text[i + VGA_COLUMNS];

    for (int i = 0; i < VGA_COLUMNS; i++)
        vga_text[VGA_COLUMNS * (VGA_ROWS - 1) + i] = VGA_WHITE | ' ';

    row = VGA_ROWS - 1;
}

void vga_init(void)
{
    for (int i = 0; i < VGA_COLUMNS * VGA_ROWS; i++)
        vga_text[i] = VGA_WHITE | ' ';

    column = 0;
    row = 0;
    update_cursor();
}

void vga_putc(char c)
{
    if (c == '\n') {
        column = 0;
        row++;
    } else if (c == '\b') {
        if (column > 0)
            vga_text[row * VGA_COLUMNS + --column] = VGA_WHITE | ' ';
    } else if (c != '\r') {
        vga_text[row * VGA_COLUMNS + column] = VGA_WHITE | (uint8_t)c;
        if (++column == VGA_COLUMNS) {
            column = 0;
            row++;
        }
    }

    if (row == VGA_ROWS)
        scroll();

    update_cursor();
}

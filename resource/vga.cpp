// VGA text mode driver: 80x25 characters at physical address 0xB8000.
//
// Programs control the screen by printing special "escape codes", the same ones
// a Linux terminal uses, so the text editor also works over the serial port:
//   ESC [ row ; column H   move the cursor (counting from 1)
//   ESC [ 2 J              clear the whole screen
//   ESC [ K                clear to the end of the line
//   ESC [ n C              move the cursor right n places
//   ESC [ n D              move the cursor left n places
//   ESC [ 7 m              reverse colours, for the editor's status bar
//   ESC [ 0 m              normal colours

#include "console.h"
#include "hal.h"
#include "kstring.h"
#include "memory.h"

#define VGA_COLUMNS 80
#define VGA_ROWS    25
#define VGA_WHITE   0x0F00
#define VGA_REVERSE 0x7000

#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA  0x3D5
#define CURSOR_HIGH    0x0E
#define CURSOR_LOW     0x0F

#define ESCAPE_KEY 27
#define MAX_ESCAPE_NUMBERS 2

enum EscapeState {
    NORMAL_TEXT,
    GOT_ESCAPE,
    READING_NUMBERS,
};

static volatile uint16_t *const vga_text = (volatile uint16_t *)phys_to_virt(0xB8000);

static int column;
static int row;
static uint16_t color = VGA_WHITE;

static EscapeState escape_state = NORMAL_TEXT;
static int escape_numbers[MAX_ESCAPE_NUMBERS];
static int escape_number_count;

static void update_cursor()
{
    uint16_t position = row * VGA_COLUMNS + column;

    outb(VGA_CRTC_INDEX, CURSOR_HIGH);
    outb(VGA_CRTC_DATA, position >> 8);
    outb(VGA_CRTC_INDEX, CURSOR_LOW);
    outb(VGA_CRTC_DATA, position & 0xFF);
}

static void scroll()
{
    for (int i = 0; i < VGA_COLUMNS * (VGA_ROWS - 1); i++)
        vga_text[i] = vga_text[i + VGA_COLUMNS];

    for (int i = 0; i < VGA_COLUMNS; i++)
        vga_text[VGA_COLUMNS * (VGA_ROWS - 1) + i] = VGA_WHITE | ' ';

    row = VGA_ROWS - 1;
}

static void clear_screen()
{
    for (int i = 0; i < VGA_COLUMNS * VGA_ROWS; i++)
        vga_text[i] = VGA_WHITE | ' ';
}

static void clear_to_end_of_line()
{
    for (int i = column; i < VGA_COLUMNS; i++)
        vga_text[row * VGA_COLUMNS + i] = color | ' ';
}

static void run_escape_command(char command)
{
    int first = escape_numbers[0];
    int second = escape_numbers[1];

    if (command == 'H') {
        row = (first > 0) ? first - 1 : 0;
        column = (second > 0) ? second - 1 : 0;

        if (row >= VGA_ROWS)
            row = VGA_ROWS - 1;
        if (column >= VGA_COLUMNS)
            column = VGA_COLUMNS - 1;
    } else if (command == 'C' || command == 'D') {
        int amount = (first > 0) ? first : 1;
        int position = row * VGA_COLUMNS + column;

        // A long line wraps, so moving can cross onto the next row.
        position += (command == 'C') ? amount : -amount;

        if (position >= 0 && position < VGA_COLUMNS * VGA_ROWS) {
            row = position / VGA_COLUMNS;
            column = position % VGA_COLUMNS;
        }
    } else if (command == 'J') {
        clear_screen();
    } else if (command == 'K') {
        clear_to_end_of_line();
    } else if (command == 'm') {
        color = (first == 7) ? VGA_REVERSE : VGA_WHITE;
    }
}

// Reads one character of an escape code. Returns false if `c` is normal text.
static bool handle_escape(char c)
{
    if (escape_state == NORMAL_TEXT) {
        if (c != ESCAPE_KEY)
            return false;

        escape_state = GOT_ESCAPE;
        return true;
    }

    if (escape_state == GOT_ESCAPE) {
        if (c == '[') {
            escape_state = READING_NUMBERS;
            escape_number_count = 0;
            escape_numbers[0] = 0;
            escape_numbers[1] = 0;
        } else {
            escape_state = NORMAL_TEXT;
        }
        return true;
    }

    if (c >= '0' && c <= '9') {
        if (escape_number_count < MAX_ESCAPE_NUMBERS) {
            int *number = &escape_numbers[escape_number_count];

            *number = *number * 10 + (c - '0');
        }
    } else if (c == ';') {
        escape_number_count++;
    } else {
        run_escape_command(c);
        escape_state = NORMAL_TEXT;
    }
    return true;
}

void vga_init()
{
    clear_screen();
    column = 0;
    row = 0;
    update_cursor();
}

void vga_putc(char c)
{
    if (handle_escape(c)) {
        update_cursor();
        return;
    }

    if (c == '\n') {
        column = 0;
        row++;
    } else if (c == '\b') {
        if (column > 0) {
            column--;
            vga_text[row * VGA_COLUMNS + column] = VGA_WHITE | ' ';
        }
    } else if (c != '\r') {
        vga_text[row * VGA_COLUMNS + column] = color | (uint8_t)c;
        column++;
        if (column == VGA_COLUMNS) {
            column = 0;
            row++;
        }
    }

    if (row == VGA_ROWS)
        scroll();

    update_cursor();
}

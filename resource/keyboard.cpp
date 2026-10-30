// PS/2 keyboard driver and the input event queue. The interrupt handler only
// turns a scancode into a character and queues it. A process then reads the
// queue, sleeping when it's empty.

#include "hal.h"
#include "input.h"
#include "process.h"

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_IRQ 1

#define RELEASE_BIT 0x80
#define SCANCODE_LEFT_SHIFT  0x2A
#define SCANCODE_RIGHT_SHIFT 0x36
#define SCANCODE_CTRL        0x1D
#define SCANCODE_EXTENDED    0xE0

#define QUEUE_SIZE 256

// Scancode set 1, US layout. 0 means the key has no character.
static const char normal_keys[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ',
};

static const char shifted_keys[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ',
};

static char queue[QUEUE_SIZE];
static volatile uint32_t queue_head;
static volatile uint32_t queue_count;
static WaitQueue readers;

static bool shift_down;
static bool ctrl_down;
static bool extended_key;

void input_put_char(char c)
{
    if (queue_count == QUEUE_SIZE)
        return;  // nobody is reading, so drop the key

    queue[(queue_head + queue_count) % QUEUE_SIZE] = c;
    queue_count = queue_count + 1;
    readers.wake_all();
}

char input_get_char()
{
    uint32_t flags = turn_off_interrupts();
    char c;

    while (queue_count == 0)
        readers.sleep();

    c = queue[queue_head];
    queue_head = (queue_head + 1) % QUEUE_SIZE;
    queue_count = queue_count - 1;

    restore_interrupts(flags);
    return c;
}

static void keyboard_interrupt(Registers *r)
{
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    bool released = scancode & RELEASE_BIT;
    uint8_t key = scancode & ~RELEASE_BIT;
    char c;

    (void)r;

    if (scancode == SCANCODE_EXTENDED) {
        extended_key = true;
        return;
    }
    if (extended_key) {
        extended_key = false;  // arrow keys and so on have no character
        return;
    }

    if (key == SCANCODE_LEFT_SHIFT || key == SCANCODE_RIGHT_SHIFT) {
        shift_down = !released;
        return;
    }
    if (key == SCANCODE_CTRL) {
        ctrl_down = !released;
        return;
    }
    if (released)
        return;

    c = shift_down ? shifted_keys[key] : normal_keys[key];
    if (c == 0)
        return;

    if (ctrl_down && c >= 'a' && c <= 'z')
        c = c - 'a' + 1;  // Ctrl+D becomes character 4

    input_put_char(c);
}

static void serial_received(char c)
{
    input_put_char(c);
}

void keyboard_init()
{
    set_interrupt_handler(VECTOR_IRQ_BASE + KEYBOARD_IRQ, keyboard_interrupt);
    pic_unmask(KEYBOARD_IRQ);

    serial_enable_input(serial_received);
}

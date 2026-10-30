// PS/2 mouse driver. The mouse sends 3 bytes for each movement, one interrupt per
// byte: the buttons, then how far it moved in x and in y. We collect the 3 bytes
// and put one MouseEvent in a queue, which programs read from /dev/mouse.

#include "hal.h"
#include "input.h"
#include "process.h"

#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define PS2_COMMAND 0x64

#define STATUS_DATA_WAITING 0x01
#define STATUS_BUSY 0x02

#define COMMAND_ENABLE_MOUSE_PORT 0xA8
#define COMMAND_READ_SETTINGS 0x20
#define COMMAND_WRITE_SETTINGS 0x60
#define COMMAND_SEND_TO_MOUSE 0xD4

#define MOUSE_USE_DEFAULTS 0xF6
#define MOUSE_START_REPORTING 0xF4
#define MOUSE_OK_REPLY 0xFA

#define SETTING_MOUSE_INTERRUPT 0x02
#define SETTING_MOUSE_CLOCK_OFF 0x20

#define MOUSE_IRQ 12
#define BYTES_PER_PACKET 3
#define MAX_EVENTS 32

#define BUTTON_BITS 0x07
#define X_IS_NEGATIVE 0x10
#define Y_IS_NEGATIVE 0x20
#define ALWAYS_SET_BIT 0x08

static MouseEvent events[MAX_EVENTS];
static volatile uint32_t first_event;
static volatile uint32_t event_count;
static WaitQueue readers;

static uint8_t packet[BYTES_PER_PACKET];
static int bytes_received;

static void wait_until_ready_to_send()
{
    while (inb(PS2_STATUS) & STATUS_BUSY)
        ;
}

static void wait_for_data()
{
    while ((inb(PS2_STATUS) & STATUS_DATA_WAITING) == 0)
        ;
}

static void send_command(uint8_t command)
{
    wait_until_ready_to_send();
    outb(PS2_COMMAND, command);
}

static void send_to_mouse(uint8_t value)
{
    send_command(COMMAND_SEND_TO_MOUSE);
    wait_until_ready_to_send();
    outb(PS2_DATA, value);
    wait_for_data();
    inb(PS2_DATA);              // the mouse answers with "OK"
}

static void add_event()
{
    if (event_count == MAX_EVENTS)
        return;                 // nobody is reading, so drop it

    MouseEvent *event = &events[(first_event + event_count) % MAX_EVENTS];

    event->buttons = packet[0] & BUTTON_BITS;

    // The movement is 9 bits: 8 in its own byte, and the sign in the first byte.
    event->x_move = packet[1];
    event->y_move = packet[2];
    if (packet[0] & X_IS_NEGATIVE)
        event->x_move -= 256;
    if (packet[0] & Y_IS_NEGATIVE)
        event->y_move -= 256;

    event_count = event_count + 1;
    readers.wake_all();
}

static void mouse_interrupt(Registers *)
{
    uint8_t value = inb(PS2_DATA);

    // The first byte always has this bit set. If not, we lost our place. A late
    // "OK" reply from the mouse is not movement either.
    if (bytes_received == 0 && ((value & ALWAYS_SET_BIT) == 0 || value == MOUSE_OK_REPLY))
        return;

    packet[bytes_received] = value;
    bytes_received++;

    if (bytes_received == BYTES_PER_PACKET) {
        bytes_received = 0;
        add_event();
    }
}

bool mouse_get_event(MouseEvent *result)
{
    uint32_t flags = turn_off_interrupts();

    while (event_count == 0) {
        readers.sleep(true);
        if (signal_pending()) {
            restore_interrupts(flags);
            return false;
        }
    }

    *result = events[first_event];
    first_event = (first_event + 1) % MAX_EVENTS;
    event_count = event_count - 1;

    restore_interrupts(flags);
    return true;
}

void mouse_init()
{
    // The keyboard and mouse share one data port. Interrupts must stay off here,
    // or the keyboard handler would take the replies we are waiting for.
    uint32_t flags = turn_off_interrupts();

    // Throw away anything old, or every reply below would be read one byte late.
    while (inb(PS2_STATUS) & STATUS_DATA_WAITING)
        inb(PS2_DATA);

    send_command(COMMAND_ENABLE_MOUSE_PORT);

    send_command(COMMAND_READ_SETTINGS);
    wait_for_data();
    uint8_t settings = inb(PS2_DATA);

    settings = (settings | SETTING_MOUSE_INTERRUPT) & ~SETTING_MOUSE_CLOCK_OFF;
    send_command(COMMAND_WRITE_SETTINGS);
    wait_until_ready_to_send();
    outb(PS2_DATA, settings);

    send_to_mouse(MOUSE_USE_DEFAULTS);
    send_to_mouse(MOUSE_START_REPORTING);

    set_interrupt_handler(VECTOR_IRQ_BASE + MOUSE_IRQ, mouse_interrupt);
    pic_unmask(MOUSE_IRQ);

    restore_interrupts(flags);
}

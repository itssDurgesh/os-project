// The real-time clock: a small chip with a battery that keeps the date and time
// while the computer is off. QEMU sets it from the laptop's clock, in UTC.

#include "hal.h"

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

#define REGISTER_SECOND   0x00
#define REGISTER_MINUTE   0x02
#define REGISTER_HOUR     0x04
#define REGISTER_DAY      0x07
#define REGISTER_MONTH    0x08
#define REGISTER_YEAR     0x09
#define REGISTER_STATUS_A 0x0A
#define REGISTER_STATUS_B 0x0B

#define UPDATE_IN_PROGRESS 0x80
#define BINARY_NUMBERS     0x04
#define CENTURY 2000

static uint8_t read_register(uint8_t number)
{
    outb(CMOS_ADDRESS, number);
    return inb(CMOS_DATA);
}

// The chip normally stores each decimal digit in 4 bits, so 0x59 means 59.
static int from_decimal_digits(uint8_t value)
{
    return (value >> 4) * 10 + (value & 0x0F);
}

void rtc_read(RealTime *time)
{
    // Reading while the chip updates itself could mix the old and new time.
    while (read_register(REGISTER_STATUS_A) & UPDATE_IN_PROGRESS)
        ;

    uint8_t second = read_register(REGISTER_SECOND);
    uint8_t minute = read_register(REGISTER_MINUTE);
    uint8_t hour = read_register(REGISTER_HOUR);
    uint8_t day = read_register(REGISTER_DAY);
    uint8_t month = read_register(REGISTER_MONTH);
    uint8_t year = read_register(REGISTER_YEAR);

    if (read_register(REGISTER_STATUS_B) & BINARY_NUMBERS) {
        time->second = second;
        time->minute = minute;
        time->hour = hour;
        time->day = day;
        time->month = month;
        time->year = CENTURY + year;
    } else {
        time->second = from_decimal_digits(second);
        time->minute = from_decimal_digits(minute);
        time->hour = from_decimal_digits(hour);
        time->day = from_decimal_digits(day);
        time->month = from_decimal_digits(month);
        time->year = CENTURY + from_decimal_digits(year);
    }
}

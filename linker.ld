ENTRY(_start)

KERNEL_VMA = 0xC0000000;

SECTIONS
{
    /* Runs before paging, so it stays at its physical address. */
    . = 1M;

    .boot :
    {
        *(.multiboot)
        *(.boot.text)
        *(.boot.data)
    }

    /* Everything else is linked in the top 1 GB of the address space. */
    . = ALIGN(4K);
    . += KERNEL_VMA;

    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VMA)
    {
        *(.text*)
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VMA)
    {
        *(.rodata*)
    }

    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VMA)
    {
        start_of_constructors = .;
        KEEP(*(.init_array*))
        end_of_constructors = .;
        *(.data*)
    }

    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_VMA)
    {
        *(COMMON)
        *(.bss*)
    }

    _kernel_end = .;

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.eh_frame*)
    }
}

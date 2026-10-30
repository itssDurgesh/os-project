; Milestone 0 entry point: a Multiboot header, a stack, and a call into C.

MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ 0x3                 ; page-align modules, provide memory info
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .text
global _start
extern kmain

_start:
    mov esp, stack_top
    call kmain

.halt:
    cli
    hlt
    jmp .halt

section .note.GNU-stack noalloc noexec nowrite progbits

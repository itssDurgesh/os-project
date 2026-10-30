; Kernel entry point. GRUB starts us here in 32-bit mode with paging off.
; We turn on a temporary page directory so the kernel can run at 0xC0000000,
; then jump into C. Memory Management replaces this directory in paging_init().

KERNEL_VMA  equ 0xC0000000

MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ 0x3                 ; page-align modules, provide memory info
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

PDE_4MB     equ 0x83                ; present, writable, 4 MB page
CR4_PSE     equ 0x10                ; allow 4 MB pages
CR0_PG_WP   equ 0x80010000          ; paging on, kernel obeys read-only pages

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM

; Maps the first 16 MB twice: at address 0 (the code below is still running
; there) and at 0xC0000000 (where the kernel is linked).
section .boot.data progbits alloc noexec nowrite align=4096
boot_page_dir:
%assign i 0
%rep 1024
    %if i < 4
        dd (i << 22) | PDE_4MB
    %elif i >= 768 && i < 772
        dd ((i - 768) << 22) | PDE_4MB
    %else
        dd 0
    %endif
    %assign i i + 1
%endrep

section .boot.text progbits alloc exec nowrite align=16
global _start

_start:
    mov ecx, cr4
    or ecx, CR4_PSE
    mov cr4, ecx

    mov ecx, boot_page_dir
    mov cr3, ecx

    mov ecx, cr0
    or ecx, CR0_PG_WP
    mov cr0, ecx

    mov ecx, higher_half
    jmp ecx

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .text
extern kmain

higher_half:
    mov esp, stack_top
    push ebx                        ; physical address of the Multiboot info
    push eax                        ; Multiboot magic number
    call kmain

.halt:
    cli
    hlt
    jmp .halt

section .note.GNU-stack noalloc noexec nowrite progbits
